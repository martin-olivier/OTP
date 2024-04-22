// SPDX-License-Identifier: GPL

#include <linux/atomic.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#define MOD_NAME "otp"
#define MAX_DEVICES 256
#define MAX_ALGO_PWD_LEN 256

static struct class *cls;
static struct proc_dir_entry *proc;

// major number assigned to the device driver
static int major;

////////////
// STATES //
////////////

enum {
	DEV_NOT_USED = 0,
	DEV_USED = 1,
};

// struct that contains one time password data
struct otp_state {
	union {
		int iterator;
		char key[16];
	}
	bool already_validated;
	atomic_t already_open;
	bool is_algo;
};

/*
 * Creates a default otp state
 */
static struct otp_state otp_state_new(void)
{
	return (struct otp_state) {
		.iterator = -1,
		.already_validated = true,
		.already_open = ATOMIC_INIT(DEV_NOT_USED),
		.is_algo = false
	};
}

struct otp_state otp_states[MAX_DEVICES];

//////////////
// ALGO VAR //
//////////////

static char *algo_pwd_list[MAX_ALGO_PWD_LEN][MAX_DEVICES] = { 0 }

////////////
// PARAMS //
////////////

static int devices = 1;
static char *pwd_list[4096] = { NULL };
static int pwd_list_argc;
static char pwd_key[4096] = { '\0' };
static int pwd_expiration = 0;

/*
 * Called when the 'devices' parameter is changed
 */
static int notify_devices_param(const char *val, const struct kernel_param *kp)
{
	int old_devices = devices;
	int res = param_set_int(val, kp);

	if (res != 0)
		return -EINVAL;

	// if devices is less than 1 or greater that MAX_DEVICES, we return an error
	if (devices < 1 || devices > MAX_DEVICES) {
		devices = old_devices;
		return -EINVAL;
	}

	// for the driver initialization, let dev_init handle the creation of devices
	if (cls == NULL)
		return 0;

	pr_info("otp: devices changed from %d to %d\n", old_devices, devices);

	// if devices is greater than old devices, we need to add devices, instead, if it is smaller, we need to remove devices
	if (devices > old_devices) {
		for (int i = old_devices; i < devices; i++) {
			device_create(cls, NULL, MKDEV(major, i), NULL, "%s%d", MOD_NAME, i);
			otp_states[i] = otp_state_new();
			pr_info("otp: device created at /dev/%s%d\n", MOD_NAME, i);
		}
	} else if (devices < old_devices) {
		for (int i = devices; i < old_devices; i++) {
			device_destroy(cls, MKDEV(major, i));
			pr_info("otp: device deleted /dev/%s%d\n", MOD_NAME, i);
		}
	}

	return 0;
}

static const struct kernel_param_ops devices_cb_ops = {
	.set = &notify_devices_param,
	.get = &param_get_int,
};

module_param_cb(devices, &devices_cb_ops, &devices, 0660);
MODULE_PARM_DESC(devices, "Number of devices to create");

module_param_array(pwd_list, charp, &pwd_list_argc, 0660);
MODULE_PARM_DESC(pwd_list, "Passwords list");

module_param(pwd_key, charp, 0660);
MODULE_PARM_DESC(pwd_key, "Encryption key (algorithm mode)");

module_param(pwd_expiration, int, 0660);
MODULE_PARM_DESC(pwd_key, "Encryption key expiration in days (algorithm mode)");


////////////
// DEVICE //
////////////

/*
 * Called when a process opens the device file
 */
static int device_open(struct inode *inode, struct file *file)
{
	int minor_number = iminor(file_inode(file));

	/* Check if the device is free to use and swap values */
	if (atomic_cmpxchg(&otp_states[minor_number].already_open, DEV_NOT_USED, DEV_USED))
		return -EBUSY;

	/* Increment the usage count */
	try_module_get(THIS_MODULE);

	return 0;
}

/*
 * Called when a process closes the device file
 */
static int device_release(struct inode *inode, struct file *file)
{
	int minor_number = iminor(file_inode(file));

	/* Set device free to use for the next caller */
	atomic_set(&otp_states[minor_number].already_open, DEV_NOT_USED);

	/* Decrement the usage count */
	module_put(THIS_MODULE);

	return 0;
}

/*
 * Called by 'device_read' when the device mode is set to list
 */
static ssize_t device_read_list(struct file *file,
				char __user *buf,
				size_t len,
				loff_t *off)
{
	size_t string_length;
	size_t bytes_to_read;

	int minor_number = iminor(file_inode(file));

	if (pwd_list_argc == 0)
		return -EINVAL;

	// If a new read occurs, we need to create a new one time password
	if (*off == 0) {
		otp_states[minor_number].iterator++;
		if (otp_states[minor_number].iterator >= pwd_list_argc)
			otp_states[minor_number].iterator = 0;
	}

	string_length = strlen(pwd_list[otp_states[minor_number].iterator]);
	bytes_to_read = min(len, (size_t)(string_length - *off));

	// If offset is beyond the end of the string, we have nothing more to read
	if (*off >= string_length) {
		otp_states[minor_number].already_validated = false;
		return 0;
	}

	// Copy data from kernel buffer to user buffer
	if (copy_to_user(buf, pwd_list[otp_states[minor_number].iterator] + *off, bytes_to_read))
		return -EFAULT;

	// Update the offset and number of bytes read
	*off += bytes_to_read;

	return bytes_to_read;
}

/*
 * Called by 'device_read' when the device mode is set to algo
 */
static ssize_t device_read_algo(struct file *file,
				char __user *buf,
				size_t len,
				loff_t *off)
{
	// TODO: to implement
	return -EINVAL;
}

/*
 * Called when a process reads from a dev file
 */
static ssize_t device_read(struct file *file,
				char __user *buf,
				size_t len,
				loff_t *off)
{
	int minor_number = iminor(file_inode(file));

	if (otp_states[minor_number].is_algo)
		return device_read_algo(file, buf, len, off);
	else
		return device_read_list(file, buf, len, off);
}

/*
 * Called by 'device_write' when the device mode is set to list
 */
static ssize_t device_write_list(struct file *file, const char __user *buf,
				size_t len, loff_t *off)
{
	static char kernel_buf[PAGE_SIZE];
	int pwd_len = 0;

	int minor_number = iminor(file_inode(file));

	// if current otp has been already validated, return EINVAL
	if (otp_states[minor_number].already_validated)
		return -EINVAL;

	// if iterator is out of range, return EINVAL
	if (otp_states[minor_number].iterator == -1 || otp_states[minor_number].iterator >= pwd_list_argc)
		return -EINVAL;

	pwd_len = strlen(pwd_list[otp_states[minor_number].iterator]);

	if (len > PAGE_SIZE)
		return -EINVAL;

	// check if the password length is the same as the incoming data
	if (len != pwd_len)
		return -EINVAL;

	// get the data from the user
	if (copy_from_user(kernel_buf, buf, len))
		return -EFAULT;

	// compare incoming data with the current password
	if (strncmp(kernel_buf, pwd_list[otp_states[minor_number].iterator], pwd_len) == 0) {
		otp_states[minor_number].already_validated = true;
		return len;
	} else
		return -EINVAL;
}

static void encrypt_key(char *key, int key_len, char *pwd, int pwd_len)
{
	const char first_pchar = '!';
	const char last_pchar_offset = '~' - first_pchar;
	for (int i = 0; i < pwd_len; i++) {
		const char encrypted_letter = pwd[i] ^ key[i % key_len];
		pwd[i] =  first_pchar + ( encrypted_letter % last_pchar_offset ) ;
	}
}

/*
 * Called by 'device_write' when the device mode is set to algo
 */
static ssize_t device_write_algo(struct file *file, const char __user *buf,
				size_t len, loff_t *off)
{
	static char kernel_buf[MAX_ALGO_PWD_LEN];
	int pwd_len = 0;

	int minor_number = iminor(file_inode(file));

	// if current otp has been already validated, return EINVAL
	if (otp_states[minor_number].already_validated)
		return -EINVAL;

	pwd_len = strlen(pwd_list[otp_states[minor_number].iterator]);

	if (len > MAX_ALGO_PWD_LEN)
		return -EINVAL;

	// check if the password length is the same as the incoming data
	if (len != pwd_len)
		return -EINVAL;

	// get the data from the user
	if (copy_from_user(kernel_buf, buf, len))
		return -EFAULT;

	// encrypt the incoming data
	encrypt_key(pwd_key, pwd_len, kernel_buf, len);

	// compare incoming data with the current password
	if (strncmp(kernel_buf, algo_pwd_list[minor_number], pwd_len) == 0) {
		otp_states[minor_number].already_validated = true;
		return len;
	} else
		return -EINVAL;
}

/*
 * Called when a process writes to a dev file
 */
static ssize_t device_write(struct file *file, const char __user *buf,
				size_t len, loff_t *off)
{
	int minor_number = iminor(file_inode(file));

	if (otp_states[minor_number].is_algo)
		return device_write_algo(file, buf, len, off);
	else
		return device_write_list(file, buf, len, off);
}

/*
 * Called when an process performs an i/o control operation to a dev file
 */
static long device_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int minor_number = iminor(file_inode(file));

	switch (cmd) {
	case 0: // Switch to password list OTP method
		otp_states[minor_number].is_algo = false;
		pr_info("Switched to password list OTP method for /dev/%s%i\n", MOD_NAME, minor_number);
		break;
	case 1: // Switch to key and time OTP method
		otp_states[minor_number].is_algo = true;
		pr_info("Switched to key and time OTP method for /dev/%s%i\n", MOD_NAME, minor_number);
		break;
	default:
		return -EINVAL; // invalid command
	}

	return 0;
}

static const struct file_operations dev_ops = {
	.read = device_read,
	.write = device_write,
	.open = device_open,
	.release = device_release,
	.unlocked_ioctl = device_ioctl,
};

////////////
// PROCFS //
////////////

/*
 * Called when a process reads the proc file
 */
static int proc_show(struct seq_file *seq, void *off)
{
	seq_printf(seq, "DEVICE     MODE     PASSWORD\n");
	seq_printf(seq, "------     ----     --------\n");

	// iterate on all devices to display their current status
	for (int i = 0; i < devices; i++) {
		int iterator = otp_states[i].iterator;
		bool validated = otp_states[i].already_validated;
		bool algo = otp_states[i].is_algo;
		
		seq_printf(seq, "%s%d%s     %s     %s\n",
			MOD_NAME,
			i,
			i < 10 ? "  " : (i < 100 ? " " : ""),
			algo ? "algo" : "list",
			algo ? "" : (
				iterator == -1 || validated ? "" : pwd_list[otp_states[i].iterator]
			)
		);
	}

	return 0;
}

//////////////////
// INIT && EXIT //
//////////////////

/*
 * Called when the module is installed
 */
static int __init dev_init(void)
{
	major = register_chrdev(0, MOD_NAME, &dev_ops);

	if (major < 0) {
		pr_alert("Registering device failed with code %d\n", major);
		return major;
	}

	pr_info("otp: major number assigned: %d\n", major);

	cls = class_create(THIS_MODULE, MOD_NAME);

	for (int i = 0; i < devices; i++) {
		device_create(cls, NULL, MKDEV(major, i), NULL, "%s%d", MOD_NAME, i);
		otp_states[i] = otp_state_new();
		pr_info("otp: device created at /dev/%s%d\n", MOD_NAME, i);
	}

	proc = proc_create_single_data(MOD_NAME, 0666, NULL, &proc_show, NULL);

	pr_info("otp: proc created at /proc/%s\n", MOD_NAME);

	return 0;
}

/*
 * Called when the module is removed
 */
static void __exit dev_exit(void)
{
	proc_remove(proc);

	for (int i = 0; i < devices; i++) {
		device_destroy(cls, MKDEV(major, i));
		pr_info("otp: device deleted /dev/%s%d\n", MOD_NAME, i);
	}
	class_destroy(cls);

	unregister_chrdev(major, MOD_NAME);

	pr_info("otp: proc deleted /proc/%s\n", MOD_NAME);
}

module_init(dev_init);
module_exit(dev_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Martin Olivier, Gabriel Medoukali, Edouard Sengeissen");
MODULE_DESCRIPTION("A one time password management kernel module");
