// SPDX-License-Identifier: GPL-2.0
/* vdaq_main.c - vDAQ module and character-device lifecycle */
#include "vdaq_internal.h"

#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

static struct vdaq_device vdaq_device;

static int vdaq_open(struct inode *inode, struct file *file)
{
	struct vdaq_device *dev;

	dev = container_of(inode->i_cdev, struct vdaq_device, cdev);
	file->private_data = dev;
	pr_info("vdaq: device opened\n");
	return 0;
}

static int vdaq_release(struct inode *inode, struct file *file)
{
	(void)inode;
	(void)file;
	pr_info("vdaq: device released\n");
	return 0;
}

static const struct file_operations vdaq_fops = {
    .owner = THIS_MODULE,
    .open = vdaq_open,
    .release = vdaq_release,
    .read = vdaq_read,
    .poll = vdaq_poll,
    .unlocked_ioctl = vdaq_ioctl,
};

static void vdaq_chrdev_unregister(struct vdaq_device *dev)
{
	device_destroy(dev->class, dev->devno);
	class_destroy(dev->class);
	cdev_del(&dev->cdev);
	unregister_chrdev_region(dev->devno, VDAQ_DEVICE_COUNT);
}

static int vdaq_chrdev_register(struct vdaq_device *dev)
{
	int ret;

	ret = alloc_chrdev_region(&dev->devno, 0, VDAQ_DEVICE_COUNT,
				  VDAQ_DEVICE_NAME);
	if (ret)
		return ret;
	cdev_init(&dev->cdev, &vdaq_fops);
	dev->cdev.owner = THIS_MODULE;
	ret = cdev_add(&dev->cdev, dev->devno, VDAQ_DEVICE_COUNT);
	if (ret)
		goto err_unregister;

	dev->class = class_create(THIS_MODULE, VDAQ_CLASS_NAME);
	if (IS_ERR(dev->class)) {
		ret = PTR_ERR(dev->class);
		goto err_del_cdev;
  }
  dev->device = device_create(dev->class, NULL, dev->devno, dev, VDAQ_DEVICE_NAME);
  
	if (IS_ERR(dev->device)) {
		ret = PTR_ERR(dev->device);
		goto err_destroy_class;
	}
	return 0;

err_destroy_class:
	class_destroy(dev->class);
err_del_cdev:
	cdev_del(&dev->cdev);
err_unregister:
	unregister_chrdev_region(dev->devno, VDAQ_DEVICE_COUNT);
	return ret;
}

static int __init vdaq_init(void)
{
	struct vdaq_device *dev = &vdaq_device;
	int ret;

	vdaq_buffer_init(dev);
	vdaq_control_init(dev);
	ret = vdaq_chrdev_register(dev);
	if (ret) {
		pr_err("vdaq: character-device registration failed: %d\n", ret);
		return ret;
	}

  ret = vdaq_sysfs_register(dev);
  if (ret) {
    pr_err("vdaq: sysfs registration failed: %d\n", ret);
    vdaq_chrdev_unregister(dev);
    return ret;
  }
  ret = vdaq_debugfs_init(dev);
  if (ret) {
    pr_err("vdaq: debugfs registration failed: %d\n", ret);
  }

  vdaq_start(dev);
  pr_info("vdaq: module initialized, major=%u minor=%u rate=%u Hz\n",
    MAJOR(dev->devno), MINOR(dev->devno), dev->sample_rate);
  return 0;
}

static void __exit vdaq_exit(void)
{
	struct vdaq_device *dev = &vdaq_device;

  pr_info("vdaq: removing module\n");
  vdaq_debugfs_exit(dev);
	vdaq_sysfs_unregister(dev);  
	vdaq_control_shutdown(dev);
	vdaq_chrdev_unregister(dev);  
	pr_info("vdaq: module removed\n");
}

module_init(vdaq_init);
module_exit(vdaq_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Komorebi");
MODULE_DESCRIPTION("Virtual multi-channel DAQ character driver");
MODULE_VERSION("0.4");
