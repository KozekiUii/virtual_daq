#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/spinlock.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/wait.h>

#define DEV_NAME "vdaq0"
#define DEV_CNT (1)
#define VDAQ_BUFF_SIZE 1024
#define VDAQ_PERIOD_NS 10000000L

/*******************************结构体存放*********************************************/
struct vdaq_sample {
  __u64 timestamp_ns; // 时间戳
  __u32 sequence;     // 序号
  __s16 channel[4];   // 4个模拟通道
  __u16 status;       // 状态，0表示正常
};

// 全局设备结构
struct vdaq_dev {
  struct vdaq_sample buf[1024];
  int head; // 环形缓冲区写指针
  int tail; // 环形缓冲区读指针
  spinlock_t lock;
  ktime_t period;
  wait_queue_head_t wq;
  struct hrtimer timer;
  __u32 seq;
  __u64 generate_samples;
  __u64 dropped_samples;
};

// 数据结构
struct vdaq_device {
  dev_t devno;
  struct cdev cdev;
  struct class *class;
  struct device *device;
};
/*******************************结构体存放*********************************************/

/*******************************变量初始化**********************************************/
static struct vdaq_device vdaq;
static struct vdaq_dev vdev;
static char vbuf[VDAQ_BUFF_SIZE];
/*******************************变量初始化**********************************************/

/*******************************函数声明**********************************************/
static int vdaq_open(struct inode *inode, struct file *filp);
static int vdaq_release(struct inode *inode, struct file *filp);
static ssize_t vdaq_write(struct file *filp, const char __user *buf,
                          size_t count, loff_t *ppos);
static ssize_t vdaq_read(struct file *filp, char __user *buf, size_t count,
                         loff_t *ppos);
static enum hrtimer_restart vdaq_timer_callback(struct hrtimer *timer);
static void write_ringBuffer(struct vdaq_dev *dev, struct vdaq_sample *sample);
static bool vdaq_ring_empty(struct vdaq_dev *dev);
static int vdaq_ring_pop(struct vdaq_dev *dev, struct vdaq_sample *sample);
/*******************************函数声明**********************************************/

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

static bool vdaq_ring_empty(struct vdaq_dev *dev) {
  if(dev->head == dev->tail) {
    return true;
  }
  return false;
}

static int vdaq_ring_pop(struct vdaq_dev *dev, struct vdaq_sample *sample) {
  unsigned long flags;
  if(vdaq_ring_empty(dev)) {
    return -1; // 缓冲区为空
  }
  spin_lock_irqsave(&dev->lock, flags);
  *sample = dev->buf[dev->tail];
  dev->tail = (dev->tail + 1) % VDAQ_BUFF_SIZE;
  spin_unlock_irqrestore(&dev->lock, flags);
  return 0; // 成功弹出数据
}

static ssize_t vdaq_write(struct file *filp, const char __user *buf,
                          size_t count, loff_t *ppos) {
  size_t write_count;

  if (*ppos >= VDAQ_BUFF_SIZE) {
    return -ENOSPC;
  }

  write_count = count;
  if (*ppos + write_count > VDAQ_BUFF_SIZE) {
    write_count = VDAQ_BUFF_SIZE - *ppos;
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
  struct vdaq_sample readData;
  // 已经读完
  if (*ppos != 0) {
    return 0;
  }
  if (count < sizeof(struct vdaq_sample)) {
    return -EINVAL;
  }
  // 如果环形缓冲区为空，则阻塞等待
  wait_event_interruptible(vdev.wq, !vdaq_ring_empty(&vdev));
  // 从环形缓冲区中弹出数据
  if (vdaq_ring_pop(&vdev, &readData) < 0) {
    return -EFAULT;
  }
  // 将数据拷贝到用户空间
  if (copy_to_user(buf, &readData, sizeof(struct vdaq_sample))) {
    return -EFAULT;
  }
  *ppos += sizeof(struct vdaq_sample);

  return sizeof(struct vdaq_sample);
}

// 将采样数据写入环形缓冲区
static void write_ringBuffer(struct vdaq_dev *dev, struct vdaq_sample *sample) {
  unsigned long flags;
  spin_lock_irqsave(&dev->lock, flags);
  dev->buf[dev->head] = *sample;
  dev->head = (dev->head + 1) % VDAQ_BUFF_SIZE;
  if (dev->head == dev->tail) {
    // 缓冲区满，覆盖最旧的数据
    dev->tail = (dev->tail + 1) % VDAQ_BUFF_SIZE;
    dev->dropped_samples++;
  }
  spin_unlock_irqrestore(&dev->lock, flags);
  wake_up_interruptible(&dev->wq); // 唤醒等待队列中的进程
}
  

// hrtimer回调函数
static enum hrtimer_restart vdaq_timer_callback(struct hrtimer *timer) {
  struct vdaq_dev *dev;
  struct vdaq_sample sample;
  // 通过 hrtimer 成员地址反推出所属的 struct vdaq_dev 对象
  dev = container_of(timer, struct vdaq_dev, timer);
  // 生成采样数据
  sample.timestamp_ns = ktime_get_ns();
  sample.sequence = dev->seq++;
  sample.channel[0] = sample.sequence;
  sample.channel[1] = sample.sequence * 2;
  sample.channel[2] = -sample.sequence;
  sample.channel[3] = sample.sequence % 100;
  sample.status = 0; // 状态正常
  dev->generate_samples++;
  // 写入环形缓冲区
  write_ringBuffer(dev, &sample);

  if (dev->seq % 100 == 0)
    printk(KERN_INFO "dev.seq=%u, generate_samples=%llu, dropped_samples=%llu, head=%d, tail=%d\n", dev->seq, dev->generate_samples, dev->dropped_samples, dev->head, dev->tail);
  // 基于当前时间,timer之后一个period再次callback
  hrtimer_forward_now(timer, dev->period);

  return HRTIMER_RESTART; // 定时器触发后继续运行
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

  vdaq.class = class_create(THIS_MODULE,DEV_NAME);
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

  // hrtimer启动 建议在所有资源创建成功之后
  vdev.head = 0;
  vdev.tail = 0;
  vdev.seq = 0;
  vdev.generate_samples = 0;
  vdev.dropped_samples = 0;
  spin_lock_init(&vdev.lock);
  init_waitqueue_head(&vdev.wq);
  // 设置 period = 10ms
  vdev.period = ktime_set(0, VDAQ_PERIOD_NS);
  // hrtimer初始化
  hrtimer_init(&vdev.timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
  // 设置回调函数
  vdev.timer.function = vdaq_timer_callback;
  // 启动hrtimer, 每过period后触发回调函数
  hrtimer_start(&vdev.timer, vdev.period, HRTIMER_MODE_REL);

  printk(KERN_INFO "vdaq: Module initialized successfully\n");
  return 0;

device_err:
  class_destroy(vdaq.class);
class_err:
  cdev_del(&vdaq.cdev);
add_err:
  unregister_chrdev_region(vdaq.devno, DEV_CNT);
alloc_err:
  return ret;
}

static void __exit vdaq_exit(void) {
  printk(KERN_INFO "vdaq: Removing module\n");
  // 取消hrtimer, 一般最先取消
  hrtimer_cancel(&vdev.timer);

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
