/* ************************************************************************
> File Name:     vdaq.c
> Author:        Komorebi
> Created Time:  2026年07月04日 星期六 11时38分52秒
> Description:   
 ************************************************************************/
#include "vdaq_uapi.h"
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/compiler.h>

#define DEV_NAME "vdaq0"
#define DEV_CNT (1)
#define VDAQ_BUFF_SIZE 1024
#define VDAQ_PERIOD_NS 10000000L

/*******************************结构体存放*********************************************/

// 全局设备结构
struct vdaq_dev {
  struct vdaq_sample buf[VDAQ_BUFF_SIZE];
  int head; // 环形缓冲区写指针
  int tail; // 环形缓冲区读指针
  spinlock_t lock;
  ktime_t period;
  unsigned int sample_rate; // 采样率
  struct mutex ctrl_lock; // 控制命令锁
  wait_queue_head_t wq;
  struct hrtimer timer;
  __u32 seq;
  __u64 generated_samples;
  __u64 dropped_samples;
  __u64 buffer_overflows;
  __u64 read_samples; // 读取的样本数
  bool running;
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
/*******************************变量初始化**********************************************/

/*******************************函数声明**********************************************/
static int vdaq_open(struct inode *inode, struct file *filp);
static int vdaq_release(struct inode *inode, struct file *filp);
static ssize_t vdaq_read(struct file *filp, char __user *buf, size_t count,
                         loff_t *ppos);
static enum hrtimer_restart vdaq_timer_callback(struct hrtimer *timer);
static void vdaq_ring_push(struct vdaq_dev *dev, struct vdaq_sample *sample);
static int vdaq_ring_pop(struct vdaq_dev *dev, struct vdaq_sample *sample);
static bool vdaq_ring_empty(struct vdaq_dev *dev);
static long vdaq_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
/*******************************函数声明**********************************************/

static const struct file_operations vdaq_fops = {
    .owner = THIS_MODULE,
    .read = vdaq_read,
    .unlocked_ioctl = vdaq_ioctl,
    .open = vdaq_open,
    .release = vdaq_release,
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
  return dev->head == dev->tail;
}

static int vdaq_ring_pop(struct vdaq_dev *dev, struct vdaq_sample *sample) {
  unsigned long flags;
  spin_lock_irqsave(&dev->lock, flags);
  if (dev->head == dev->tail) {
    spin_unlock_irqrestore(&dev->lock, flags);
    return -1; // 缓冲区为空
  }
  *sample = dev->buf[dev->tail];
  dev->tail = (dev->tail + 1) % VDAQ_BUFF_SIZE;
  dev->read_samples++;
  spin_unlock_irqrestore(&dev->lock, flags);
  return 0; // 成功弹出数据
}

static ssize_t vdaq_read(struct file *filp, char __user *buf, size_t count,
                         loff_t *ppos) {
  struct vdaq_sample readData;
  if (count < sizeof(struct vdaq_sample)) {
    return -EINVAL;
  }
  // 如果环形缓冲区为空，则阻塞等待
  if (wait_event_interruptible(vdev.wq, !vdaq_ring_empty(&vdev) || !vdev.running)) {
    return -EINTR; // 如果被信号中断，则返回错误
  }

  if (vdaq_ring_empty(&vdev) && !vdev.running) {
    return -EFAULT; // 再次检查环形缓冲区是否为空
  }
  // 从环形缓冲区中弹出数据
  if (vdaq_ring_pop(&vdev, &readData) < 0) {
    return -EAGAIN;
  }
  // 将数据拷贝到用户空间
  if (copy_to_user(buf, &readData, sizeof(struct vdaq_sample))) {
    return -EFAULT;
  }
  // 不需要再增加 *ppos，因为是从环形缓冲区中读取数据，而不是从文件中读取数据
  // *ppos += sizeof(struct vdaq_sample);

  return sizeof(struct vdaq_sample);
}

static long vdaq_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
  switch (cmd) {
    case VDAQ_IOCTL_GET_STATUS: {
      struct vdaq_stats stats;
      unsigned long flags;

      spin_lock_irqsave(&vdev.lock, flags);
      stats.generated_samples = vdev.generated_samples;
      stats.read_samples = vdev.read_samples;
      stats.dropped_samples = vdev.dropped_samples;
      stats.buffer_overflows = vdev.buffer_overflows;
      stats.current_sequence = vdev.seq;
      stats.buffer_head = vdev.head;
      stats.buffer_tail = vdev.tail;
      stats.running = vdev.running;

      spin_unlock_irqrestore(&vdev.lock, flags);

      if (copy_to_user((void __user *)arg, &stats, sizeof(stats))) {
        return -EFAULT;
      }
      break;
    }
    case VDAQ_IOCTL_STOP: {
      mutex_lock(&vdev.ctrl_lock);
      if (vdev.running) {
        vdev.running = false;
        hrtimer_cancel(&vdev.timer);
        wake_up_interruptible(&vdev.wq); // 唤醒等待队列中的进程
      }
      mutex_unlock(&vdev.ctrl_lock);
      break;
    }
    case VDAQ_IOCTL_START: {
      mutex_lock(&vdev.ctrl_lock);
      if (!vdev.running) {
        vdev.running = true;
        hrtimer_start(&vdev.timer, vdev.period, HRTIMER_MODE_REL);
      }
      mutex_unlock(&vdev.ctrl_lock);        
      break;
    }
    case VDAQ_IOCTL_CLEAR_BUFFER: {
      unsigned long flags;
      spin_lock_irqsave(&vdev.lock, flags);
      vdev.head = 0;
      vdev.tail = 0;
      spin_unlock_irqrestore(&vdev.lock, flags);
      break;
    }
    case VDAQ_IOCTL_SET_RATE: {
      unsigned int new_rate;
      ktime_t new_period;
      if (copy_from_user(&new_rate, (unsigned int __user *)arg,
                         sizeof(new_rate))) {
        return -EFAULT;
      }
      if (new_rate > 10000 || new_rate < 1) {
        return -EINVAL;
      }
      new_period = ktime_set(0, NSEC_PER_SEC/new_rate);
      
      mutex_lock(&vdev.ctrl_lock);
      hrtimer_cancel(&vdev.timer);
      WRITE_ONCE(vdev.period,new_period);
      vdev.sample_rate = new_rate;
      if (vdev.running) {
        hrtimer_start(&vdev.timer, vdev.period, HRTIMER_MODE_REL);
      }
      mutex_unlock(&vdev.ctrl_lock);
      break;
    }
    default:
      return -ENOTTY;
    }
  return 0;
}


// 将采样数据写入环形缓冲区
static void vdaq_ring_push(struct vdaq_dev *dev, struct vdaq_sample *sample) {
  unsigned long flags;
  spin_lock_irqsave(&dev->lock, flags);
  
  sample->sequence = dev->seq++;
  sample->channel[0] = sample->sequence;
  sample->channel[1] = sample->sequence * 2;
  sample->channel[2] = -sample->sequence;
  sample->channel[3] = sample->sequence % 100;
  dev->generated_samples++;

  dev->buf[dev->head] = *sample;
  dev->head = (dev->head + 1) % VDAQ_BUFF_SIZE;
  if (dev->head == dev->tail) {
    // 缓冲区满，覆盖最旧的数据
    dev->tail = (dev->tail + 1) % VDAQ_BUFF_SIZE;
    dev->dropped_samples++;
    dev->buffer_overflows++;
  }
  spin_unlock_irqrestore(&dev->lock, flags);
  wake_up_interruptible(&dev->wq); // 唤醒等待队列中的进程
}

// hrtimer回调函数
static enum hrtimer_restart vdaq_timer_callback(struct hrtimer *timer) {
  struct vdaq_dev *dev;
  struct vdaq_sample sample;
  unsigned long flags;
  ktime_t period;
  // 通过 hrtimer 成员地址反推出所属的 struct vdaq_dev 对象
  dev = container_of(timer, struct vdaq_dev, timer);

  spin_lock_irqsave(&dev->lock, flags);
  if (!dev->running) {
    spin_unlock_irqrestore(&dev->lock, flags);
    return HRTIMER_NORESTART; // 如果设备未运行，则不重新启动定时器
  }
  spin_unlock_irqrestore(&dev->lock, flags);

  // 生成采样数据
  sample.timestamp_ns = ktime_get_ns();
  sample.status = 0; // 状态正常
  
  // 写入环形缓冲区
  vdaq_ring_push(dev, &sample);

  if (dev->seq % 100 == 0)
    printk(KERN_INFO "dev.seq=%u, generate_samples=%llu, dropped_samples=%llu, "
                     "head=%d, tail=%d\n",
           dev->seq, dev->generated_samples, dev->dropped_samples, dev->head,
           dev->tail);

  // 基于当前时间,timer之后一个period再次callback
  period = READ_ONCE(dev->period); // 避免直接访问共享变量, ktime_t在ARM64上读取时原子的，READ_ONCE防止编译器优化，从内存中读取真实数据
  hrtimer_forward_now(timer, period);

  return HRTIMER_RESTART; // 定时器触发后继续运行
}

static int __init vdaq_init(void) {
  int ret = 0;
  
  // 1. vdev初始化
  vdev.head = 0;
  vdev.tail = 0;
  vdev.seq = 0;
  vdev.read_samples = 0;
  vdev.buffer_overflows = 0;
  vdev.generated_samples = 0;
  vdev.dropped_samples = 0;
  vdev.running = true;
  vdev.sample_rate = 100; // 默认采样率 100Hz
  spin_lock_init(&vdev.lock);
  mutex_init(&vdev.ctrl_lock);
  init_waitqueue_head(&vdev.wq);
  // 设置 period = 10ms
  vdev.period = ktime_set(0, VDAQ_PERIOD_NS);

  // 2. 创建设备节点

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

  vdaq.class = class_create(THIS_MODULE, DEV_NAME);
  // vdaq.class = class_create(THIS_MODULE, DEV_NAME); // C90标准

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

  
  // 3. hrtimer启动 建议在所有资源创建成功之后
  // hrtimer初始化
  // hrtimer_setup(&vdev.timer, vdaq_timer_callback, CLOCK_MONOTONIC,
  // HRTIMER_MODE_REL);
  // // 设置回调函数, 内核6.14版本之前需手动设置回调函数,
  hrtimer_init(&vdev.timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
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
