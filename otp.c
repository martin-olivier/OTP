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

/* Parameters */
static int list_argc;
static char *list[4096] = { NULL };

static int iterator = -1;
static bool already_validated = true;

module_param_array(list, charp, &list_argc, 0660);
MODULE_PARM_DESC(list, "Passwords list");

/* Prototypes */
static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);
static ssize_t device_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t device_write(struct file *, const char __user *, size_t, loff_t *);

#define DEVICE_NAME "otp"

/* Major number assigned to the device driver */
static int major;

enum {
	DEV_NOT_USED = 0,
	DEV_USED = 1,
};

/* Prevent multiple access to device */
static atomic_t already_open = ATOMIC_INIT(DEV_NOT_USED);

static struct class *cls;

static struct file_operations dev_fops = {
	.read = device_read,
	.write = device_write,
	.open = device_open,
	.release = device_release,
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

	device_create(cls, NULL, MKDEV(major, 0), NULL, DEVICE_NAME);

	pr_info("otp: device created at /dev/%s\n", DEVICE_NAME);

	return 0;
}

static void __exit dev_exit(void)
{
	device_destroy(cls, MKDEV(major, 0));
	class_destroy(cls);

	unregister_chrdev(major, DEVICE_NAME);
}

/* Methods */

/*
 * Called when a process opens the device file
 */
static int device_open(struct inode *inode, struct file *file)
{
	if (atomic_cmpxchg(&already_open, DEV_NOT_USED, DEV_USED))
		return -EBUSY;

	try_module_get(THIS_MODULE);

	return 0;
}

/*
 * Called when a process closes the device file
 */
static int device_release(struct inode *inode, struct file *file)
{
	/* Set device free to use for the next caller */
	atomic_set(&already_open, DEV_NOT_USED);

	/* Decrement the usage count */
	module_put(THIS_MODULE);

	return 0;
}

/*
 * Called when a process reads from the dev file
 */
static ssize_t device_read(struct file *filp,
				char __user *buffer,
				size_t length,
				loff_t *offset)
{
	size_t string_length;
	size_t bytes_to_read;

	if (list_argc == 0)
		return -EINVAL;

	// If a new read occurs, we need to create a new one time password
	if (*offset == 0) {
		iterator++;
		if (iterator >= list_argc)
			iterator = 0;
	}

	string_length = strlen(list[iterator]);
	bytes_to_read = min(length, (size_t)(string_length - *offset));

	// If offset is beyond the end of the string, we have nothing more to read
	if (*offset >= string_length) {
		already_validated = false;
		return 0;
	}

	// Copy data from kernel buffer to user buffer
	if (copy_to_user(buffer, list[iterator] + *offset, bytes_to_read))
		return -EFAULT;

	// Update the offset and number of bytes read
	*offset += bytes_to_read;

	return bytes_to_read;
}

/*
 * Called when a process writes to the dev file
 */
static ssize_t device_write(struct file *filp, const char __user *buff,
				size_t len, loff_t *off)
{
	static char kernel_buf[PAGE_SIZE];
	int pwd_len = 0;

	if (already_validated)
		return -EINVAL;

	if (iterator == -1 || iterator >= list_argc)
		return -EINVAL;

	pwd_len = strlen(list[iterator]);

	if (len > PAGE_SIZE)
		return -EINVAL;

	// check if the password length is the same as the incoming data
	if (len != pwd_len)
		return -EINVAL;

	// get the data from the user
	if (copy_from_user(kernel_buf, buff, len))
		return -EFAULT;

	// compare incoming data with the current password
	if (strncmp(kernel_buf, list[iterator], pwd_len) == 0) {
		already_validated = true;
		return len;
	} else
		return -EINVAL;
}

module_init(dev_init);
module_exit(dev_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Martin Olivier, Gabriel Medoukali, Edouard Sengeissen");
MODULE_DESCRIPTION("A one time password management kernel module");
