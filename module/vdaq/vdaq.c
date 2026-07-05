#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>

#define DEV_NAME "vdaq0"
#define DEV_CNT (1)
#define BUFF_SIZE 1024

struct vdaq_sample {
  __u64 timestamp_ns; // 时间戳
  __u32 sequence;     // 序号
  __s16 channel[4];   // 4个模拟通道
  __u16 status;       // 状态，0表示正常
};

struct vdaq_device {
  dev_t devno;
  struct cdev cdev;
  struct class *class;
  struct device *device;
};

static struct vdaq_device vdaq;
static char vbuf[BUFF_SIZE];

static int vdaq_open(struct inode *inode, struct file *filp);
static int vdaq_release(struct inode *inode, struct file *filp);
static ssize_t vdaq_write(struct file *filp, const char __user *buf,
                          size_t count, loff_t *ppos);
static ssize_t vdaq_read(struct file *filp, char __user *buf, size_t count,
                         loff_t *ppos);

static struct file_operations vdaq_fops = {
    .owner = THIS_MODULE,
    .open = vdaq_open,
    .release = vdaq_release,
    .write = vdaq_write,
    .read = vdaq_read,
};

static int vdaq_open(struct inode *inode, struct file *filp) {
  printk(KERN_INFO "vdaq: Device opened\n");
  return 0;
}

static int vdaq_release(struct inode *inode, struct file *filp) {
  printk(KERN_INFO "vdaq: Device released\n");
  return 0;
}

static ssize_t vdaq_write(struct file *filp, const char __user *buf,
                          size_t count, loff_t *ppos) {
  size_t write_count;

  if (*ppos >= BUFF_SIZE) {
    return -ENOSPC;
  }

  write_count = count;
  if (*ppos + write_count > BUFF_SIZE) {
    write_count = BUFF_SIZE - *ppos;
  }

  if (copy_from_user(vbuf + *ppos, buf, write_count)) {
    return -EFAULT;
  }

  *ppos += write_count;
  printk(KERN_INFO "vdaq: Wrote %zu bytes\n", write_count);
  return write_count; // 关键：必须返回实际写入的字节数
}

static ssize_t vdaq_read(struct file *filp, char __user *buf, size_t count,
                         loff_t *ppos) {
  // 已经读完
  if (*ppos != 0) {
    return 0;
  }
  if (count < sizeof(struct vdaq_sample)) {
    return -EINVAL;
  }
  struct vdaq_sample readData;
  readData.channel[0] = 10;
  readData.channel[1] = 20;
  readData.channel[2] = 30;
  readData.channel[3] = 40;
  readData.sequence = 1;
  readData.status = 0;
  readData.timestamp_ns = 0;
  if (copy_to_user(buf, &readData, sizeof(readData)))
    return -EFAULT;
  *ppos += sizeof(readData);
  return sizeof(readData);
}

static int __init vdaq_init(void) {
  int ret = 0;
  printk("chrdev init\n");
  ret = alloc_chrdev_region(&vdaq.devno, 0, DEV_CNT, DEV_NAME);
  if (ret < 0) {
    printk(KERN_ERR "vdaq: Failed to allocate device number\n");
    goto alloc_err;
  }
  printk(KERN_INFO "vdaq: Major=%d, Minor=%d\n", MAJOR(vdaq.devno),
         MINOR(vdaq.devno));

  cdev_init(&vdaq.cdev, &vdaq_fops);
  ret = cdev_add(&vdaq.cdev, vdaq.devno, DEV_CNT);
  if (ret < 0) {
    printk(KERN_ERR "vdaq: Failed to add cdev\n");
    goto add_err;
  }

  vdaq.class = class_create(DEV_NAME);
  if (IS_ERR(vdaq.class)) {
    ret = PTR_ERR(vdaq.class);
    printk(KERN_ERR "vdaq: Failed to create class\n");
    goto class_err;
  }

  vdaq.device = device_create(vdaq.class, NULL, vdaq.devno, NULL, DEV_NAME);
  if (IS_ERR(vdaq.device)) {
    ret = PTR_ERR(vdaq.device);
    printk(KERN_ERR "vdaq: Failed to create device\n");
    goto device_err;
  }

  printk(KERN_INFO "vdaq: Module initialized successfully\n");
  return 0;

device_err:
  unregister_chrdev_region(vdaq.devno, DEV_CNT);
  cdev_del(&vdaq.cdev);
  class_destroy(vdaq.class);
class_err:
  unregister_chrdev_region(vdaq.devno, DEV_CNT);
  cdev_del(&vdaq.cdev);
add_err:
  unregister_chrdev_region(vdaq.devno, DEV_CNT);
alloc_err:
  return ret;
}

static void __exit vdaq_exit(void) {
  printk(KERN_INFO "vdaq: Removing module\n");

  // 销毁设备
  device_destroy(vdaq.class, vdaq.devno);
  // 销毁类
  class_destroy(vdaq.class);
  // 删除 cdev
  cdev_del(&vdaq.cdev);
  // 释放设备号
  unregister_chrdev_region(vdaq.devno, DEV_CNT);

  printk(KERN_INFO "vdaq: Module removed\n");
}

module_init(vdaq_init);
module_exit(vdaq_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Komorebi");
MODULE_DESCRIPTION("Character Device Driver");
