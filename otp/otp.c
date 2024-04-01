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

#define DEVICE_NAME "otp"
#define MAX_DEVICES 256

static struct class *cls;

/* Major number assigned to the device driver */
static int major;

enum {
	DEV_NOT_USED = 0,
	DEV_USED = 1,
};

// stuct that contains one time password data
struct otp_state {
	int iterator;
	bool already_validated;
	atomic_t already_open;
	bool is_algo;
};

// one time password data array
struct otp_state otp_states[MAX_DEVICES];

/* Parameters */
static int list_argc;
static char *list[4096] = { NULL };

int count = 1;

int notify_count_param(const char *val, const struct kernel_param *kp)
{
	int old_count = count;
	int res = param_set_int(val, kp);

	if (res != 0)
		return -EINVAL;

	// if the count is less than 1 or greater that MAX_DEVICES, we return an error
	if (count < 1 || count > MAX_DEVICES) {
		count = old_count;
		return -EINVAL;
	}

	// for the driver initialization, let dev_init handle the creation of devices
	if (cls == NULL)
		return 0;

	pr_info("otp: count changed from %d to %d\n", old_count, count);

	// if the new count is greater than the old count, we need to add devices, instead, if it is smaller, we need to remove devices
	if (count > old_count) {
		for (int i = old_count; i < count; i++) {
			device_create(cls, NULL, MKDEV(major, i), NULL, "%s%d", DEVICE_NAME, i);
			otp_states[i] = (struct otp_state) {
				.iterator = -1,
				.already_validated = true,
				.already_open = ATOMIC_INIT(DEV_NOT_USED),
				.is_algo = false
			};
			pr_info("otp: device created at /dev/%s%d\n", DEVICE_NAME, i);
		}
	} else if (count < old_count) {
		for (int i = count; i < old_count; i++) {
			device_destroy(cls, MKDEV(major, i));
			pr_info("otp: device deleted /dev/%s%d\n", DEVICE_NAME, i);
		}
	}

	return 0;
}

const struct kernel_param_ops count_cb_ops = {
	.set = &notify_count_param,
	.get = &param_get_int,
};

module_param_cb(count, &count_cb_ops, &count, 0660);
MODULE_PARM_DESC(count, "Number of devices to create");

module_param_array(list, charp, &list_argc, 0660);
MODULE_PARM_DESC(list, "Passwords list");

/* Prototypes */
static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);
static ssize_t device_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t device_write(struct file *, const char __user *, size_t, loff_t *);
static long device_ioctl(struct file *, unsigned int, unsigned long);

static struct file_operations dev_fops = {
	.read = device_read,
	.write = device_write,
	.open = device_open,
	.release = device_release,
	.unlocked_ioctl = device_ioctl,
};

static int __init dev_init(void)
{
	major = register_chrdev(0, DEVICE_NAME, &dev_fops);

	if (major < 0) {
		pr_alert("Registering device failed with code %d\n", major);
		return major;
	}

	pr_info("otp: major number assigned: %d\n", major);

	cls = class_create(THIS_MODULE, DEVICE_NAME);

	for (int i = 0; i < count; i++) {
		device_create(cls, NULL, MKDEV(major, i), NULL, "%s%d", DEVICE_NAME, i);
		otp_states[i] = (struct otp_state) {
			.iterator = -1,
			.already_validated = true,
			.already_open = ATOMIC_INIT(DEV_NOT_USED),
			.is_algo = false
		};
		pr_info("otp: device created at /dev/%s%d\n", DEVICE_NAME, i);
	}

	return 0;
}

static void __exit dev_exit(void)
{
	for (int i = 0; i < count; i++) {
		device_destroy(cls, MKDEV(major, i));
		pr_info("otp: device deleted /dev/%s%d\n", DEVICE_NAME, i);
	}
	class_destroy(cls);

	unregister_chrdev(major, DEVICE_NAME);
}

/* Methods */

/*
 * Called when a process opens the device file
 */
static int device_open(struct inode *inode, struct file *file)
{
	int minor_number = iminor(file_inode(file));

	if (atomic_cmpxchg(&otp_states[minor_number].already_open, DEV_NOT_USED, DEV_USED))
		return -EBUSY;

	pr_info("otp: device opened\n");

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
 * Called when a process reads from the dev file
 */
static ssize_t device_read(struct file *file,
				char __user *buffer,
				size_t length,
				loff_t *offset)
{
	size_t string_length;
	size_t bytes_to_read;

	int minor_number = iminor(file_inode(file));

	if (list_argc == 0)
		return -EINVAL;

	// If a new read occurs, we need to create a new one time password
	if (*offset == 0) {
		otp_states[minor_number].iterator++;
		if (otp_states[minor_number].iterator >= list_argc)
			otp_states[minor_number].iterator = 0;
	}

	string_length = strlen(list[otp_states[minor_number].iterator]);
	bytes_to_read = min(length, (size_t)(string_length - *offset));

	// If offset is beyond the end of the string, we have nothing more to read
	if (*offset >= string_length) {
		otp_states[minor_number].already_validated = false;
		return 0;
	}

	// Copy data from kernel buffer to user buffer
	if (copy_to_user(buffer, list[otp_states[minor_number].iterator] + *offset, bytes_to_read))
		return -EFAULT;

	// Update the offset and number of bytes read
	*offset += bytes_to_read;

	return bytes_to_read;
}

/*
 * Called when a process writes to the dev file
 */
static ssize_t device_write(struct file *file, const char __user *buff,
				size_t len, loff_t *off)
{
	static char kernel_buf[PAGE_SIZE];
	int pwd_len = 0;

	int minor_number = iminor(file_inode(file));

	if (otp_states[minor_number].already_validated)
		return -EINVAL;

	if (otp_states[minor_number].iterator == -1 || otp_states[minor_number].iterator >= list_argc)
		return -EINVAL;

	pwd_len = strlen(list[otp_states[minor_number].iterator]);

	if (len > PAGE_SIZE)
		return -EINVAL;

	// check if the password length is the same as the incoming data
	if (len != pwd_len)
		return -EINVAL;

	// get the data from the user
	if (copy_from_user(kernel_buf, buff, len))
		return -EFAULT;

	// compare incoming data with the current password
	if (strncmp(kernel_buf, list[otp_states[minor_number].iterator], pwd_len) == 0) {
		otp_states[minor_number].already_validated = true;
		return len;
	} else
		return -EINVAL;
}

static long device_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int minor_number = iminor(file_inode(file));

	switch (cmd) {
	case 0: // Switch to password list OTP method
		otp_states[minor_number].is_algo = false;
		pr_info("Switched to password list OTP method for /dev/%s%i\n", DEVICE_NAME, minor_number);
		break;
	case 1: // Switch to key and time OTP method
		otp_states[minor_number].is_algo = true;
		pr_info("Switched to key and time OTP method for /dev/%s%i\n", DEVICE_NAME, minor_number);
		break;
	default:
		return -EINVAL; // invalid command
	}

	return 0;
}

module_init(dev_init);
module_exit(dev_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Martin Olivier, Gabriel Medoukali, Edouard Sengeissen");
MODULE_DESCRIPTION("A one time password management kernel module");
