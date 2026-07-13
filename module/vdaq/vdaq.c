// SPDX-License-Identifier: GPL-2.0
/*
 * vdaq.c - Virtual multi-channel data acquisition character driver
 *
 * Copyright (C) 2026 Komorebi
 *
 * The driver uses an hrtimer to simulate a periodic hardware sampling source.
 * Samples are stored in a ring buffer and consumed by userspace through read().
 * Device status and runtime controls are exposed through ioctl commands.
 */

#include "vdaq_uapi.h"

#include <linux/cdev.h>
#include <linux/compiler.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/hrtimer.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/poll.h>

#define VDAQ_DEVICE_NAME	"vdaq0"
#define VDAQ_DEVICE_COUNT	1
#define VDAQ_BUFFER_SIZE	1024
#define VDAQ_DEFAULT_RATE_HZ	100U
#define VDAQ_MAX_RATE_HZ	10000U
#define VDAQ_DEFAULT_PERIOD_NS	(NSEC_PER_SEC / VDAQ_DEFAULT_RATE_HZ)

/**
 * struct vdaq_dev - Runtime state of the virtual acquisition engine
 * @buffer:              Ring buffer used to store generated samples
 * @head:                Index of the next write position
 * @tail:                Index of the next read position
 * @data_lock:           Protects the ring buffer, counters and running state
 * @period:              Current hrtimer period
 * @sample_rate:         Current sampling rate in Hz
 * @control_lock:        Serializes START, STOP and SET_RATE operations
 * @read_queue:          Wait queue used by blocking readers
 * @timer:               High-resolution timer used as the sample source
 * @sequence:            Sequence number assigned to the next sample
 * @generated_samples:   Number of generated samples
 * @dropped_samples:     Number of samples discarded because the buffer was full
 * @buffer_overflows:    Number of ring-buffer overflow events
 * @read_samples:        Number of samples removed by userspace
 * @running:             True while periodic sampling is enabled
 */
struct vdaq_dev {
	struct vdaq_sample buffer[VDAQ_BUFFER_SIZE];
	unsigned int head;
	unsigned int tail;
	spinlock_t data_lock;
	ktime_t period;
	unsigned int sample_rate;
	struct mutex control_lock;
	wait_queue_head_t read_queue;
	struct hrtimer timer;
	u32 sequence;
	u64 generated_samples;
	u64 dropped_samples;
	u64 buffer_overflows;
	u64 read_samples;
	bool running;
};

/**
 * struct vdaq_chrdev - Character-device registration resources
 * @devno:  Allocated major/minor device number
 * @cdev:   Character-device object
 * @class:  Device class exported through sysfs
 * @device: Device object used to create /dev/vdaq0
 */
struct vdaq_chrdev {
	dev_t devno;
	struct cdev cdev;
	struct class *class;
	struct device *device;
};

static struct vdaq_chrdev vdaq_chrdev;
static struct vdaq_dev vdaq_device;

/* File-operation callbacks. */
static int vdaq_open(struct inode *inode, struct file *file);
static int vdaq_release(struct inode *inode, struct file *file);
static ssize_t vdaq_read(struct file *file, char __user *buf, size_t count,
                         loff_t *ppos);
static __poll_t vdaq_poll(struct file *filp, struct poll_table_struct *wait);
static long vdaq_ioctl(struct file *file, unsigned int cmd, unsigned long arg);


/* Sampling and ring-buffer helpers. */
static enum hrtimer_restart vdaq_timer_callback(struct hrtimer *timer);
static void vdaq_ring_push(struct vdaq_dev *dev, struct vdaq_sample *sample);
static int vdaq_ring_pop(struct vdaq_dev *dev, struct vdaq_sample *sample);
static bool vdaq_ring_empty(const struct vdaq_dev *dev);

static const struct file_operations vdaq_fops = {
    .owner = THIS_MODULE,
    .read = vdaq_read,
    .poll = vdaq_poll,
    .unlocked_ioctl = vdaq_ioctl,
    .open = vdaq_open,
    .release = vdaq_release,
};

static int vdaq_open(struct inode *inode, struct file *file)
{
	pr_info("vdaq: device opened\n");
	return 0;
}

static int vdaq_release(struct inode *inode, struct file *file)
{
	pr_info("vdaq: device released\n");
	return 0;
}

/**
 * vdaq_ring_empty - Check whether the ring buffer contains no samples
 * @dev: Virtual acquisition device
 *
 * The caller only needs a transient condition check. The definitive empty check
 * used before removing data is repeated under @data_lock in vdaq_ring_pop().
 */
static bool vdaq_ring_empty(const struct vdaq_dev *dev)
{
	return READ_ONCE(dev->head) == READ_ONCE(dev->tail);
}

/**
 * vdaq_ring_pop - Remove one sample from the ring buffer
 * @dev:    Virtual acquisition device
 * @sample: Destination for the removed sample
 *
 * Return: 0 on success, -ENODATA if the buffer is empty.
 */
static int vdaq_ring_pop(struct vdaq_dev *dev, struct vdaq_sample *sample)
{
  unsigned long flags;
  bool notify_hup = false;

	spin_lock_irqsave(&dev->data_lock, flags);

	if (dev->head == dev->tail) {
		spin_unlock_irqrestore(&dev->data_lock, flags);
		return -ENODATA;
	}

	*sample = dev->buffer[dev->tail];
	dev->tail = (dev->tail + 1) % VDAQ_BUFFER_SIZE;
	dev->read_samples++;

  /*
  * 设备已经停止，并且本次读取恰好取走最后一帧。
  * 此时 poll 状态将从“可读”变成“挂断”。
  */
  if (dev->head == dev->tail && !dev->running)
    notify_hup = true;
  spin_unlock_irqrestore(&dev->data_lock, flags);

  if(notify_hup)
    wake_up_interruptible_poll(&dev->read_queue, EPOLLIN | EPOLLRDNORM);
	return 0;
}

/**
 * vdaq_read - Read one sample from the acquisition stream
 *
 * The call blocks while sampling is running and the ring buffer is empty.
 * Stopping the device wakes blocked readers. If no buffered sample remains,
 * -EAGAIN is returned.
 */
static ssize_t vdaq_read(struct file *file, char __user *buf, size_t count,
			 loff_t *ppos)
{
	struct vdaq_sample sample;
	int ret;

	if (count < sizeof(sample))
		return -EINVAL;

  /*
   * 非阻塞模式：
   * 缓冲区为空时立即返回，不进入等待队列
   */
  if (vdaq_ring_empty(&vdaq_device)){
    if (!READ_ONCE(vdaq_device.running)) {
      return -EAGAIN;
    }
    if (file->f_flags & O_NONBLOCK) {
      return -EAGAIN;
    }
    ret = wait_event_interruptible(vdaq_device.read_queue,
                                   !vdaq_ring_empty(&vdaq_device) ||
                                       !READ_ONCE(vdaq_device.running));
    if (ret) {
      return ret;
    }
  }

  /*
  * STOP 可能唤醒 reader。
  * 若此时没有剩余数据，则返回 EAGAIN。
  */
  if (vdaq_ring_empty(&vdaq_device) && !READ_ONCE(vdaq_device.running)) {
    return -EAGAIN;
  }
      
  ret = vdaq_ring_pop(&vdaq_device, &sample);
  if (ret)
    return -EAGAIN;

  /* copy_to_user() 可能睡眠，因此必须在释放自旋锁后调用。 */
  if (copy_to_user(buf, &sample, sizeof(sample)))
    return -EFAULT;

  /* 数据源是持续流，不使用文件偏移量 *ppos。 */
  return sizeof(sample);
}

static __poll_t vdaq_poll(struct file *filp, struct poll_table_struct *wait) {
  __poll_t mask = 0;
  unsigned long flags;
  bool readable;
  bool running;
  // 登记等待队列，以便在数据可用时唤醒阻塞的 poll() 调用。
  poll_wait(filp, &vdaq_device.read_queue, wait);

  spin_lock_irqsave(&vdaq_device.data_lock, flags);
  readable = !vdaq_ring_empty(&vdaq_device);
  running = vdaq_device.running;
  spin_unlock_irqrestore(&vdaq_device.data_lock, flags);

  if (readable) {
    mask |= EPOLLIN | EPOLLRDNORM;
  }
  else if (!readable && !running) {
    mask |= EPOLLHUP;
  }

  return mask;
}

/**
 * vdaq_ioctl - Handle userspace control requests
 *
 * The control mutex serializes timer lifecycle changes. The spinlock protects
 * the high-frequency data path and status counters.
 */
static long vdaq_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct vdaq_dev *dev = &vdaq_device;

	switch (cmd) {
	case VDAQ_IOCTL_GET_STATUS:
	{
		struct vdaq_stats stats;
		unsigned long flags;

		spin_lock_irqsave(&dev->data_lock, flags);
		stats.generated_samples = dev->generated_samples;
		stats.read_samples = dev->read_samples;
		stats.dropped_samples = dev->dropped_samples;
		stats.buffer_overflows = dev->buffer_overflows;
		stats.current_sequence = dev->sequence;
		stats.buffer_head = dev->head;
		stats.buffer_tail = dev->tail;
		stats.running = dev->running;
		spin_unlock_irqrestore(&dev->data_lock, flags);

		if (copy_to_user((void __user *)arg, &stats, sizeof(stats)))
			return -EFAULT;
		break;
	}

	case VDAQ_IOCTL_START:
		mutex_lock(&dev->control_lock);
		if (!READ_ONCE(dev->running)) {
			WRITE_ONCE(dev->running, true);
			hrtimer_start(&dev->timer, READ_ONCE(dev->period),
				      HRTIMER_MODE_REL);
		}
		mutex_unlock(&dev->control_lock);
		break;

	case VDAQ_IOCTL_STOP:
		mutex_lock(&dev->control_lock);
		if (READ_ONCE(dev->running)) {
			WRITE_ONCE(dev->running, false);

			/*
			 * hrtimer_cancel() may wait for an active callback, so it
			 * must not be called while holding the data spinlock.
			 */
			hrtimer_cancel(&dev->timer);
			wake_up_interruptible(&dev->read_queue);
		}
		mutex_unlock(&dev->control_lock);
		break;

	case VDAQ_IOCTL_CLEAR_BUFFER:
	{
		unsigned long flags;

		spin_lock_irqsave(&dev->data_lock, flags);
		dev->head = 0;
		dev->tail = 0;
		spin_unlock_irqrestore(&dev->data_lock, flags);
		break;
	}

	case VDAQ_IOCTL_SET_RATE:
	{
		unsigned int new_rate;
		ktime_t new_period;

		if (copy_from_user(&new_rate, (unsigned int __user *)arg,
				   sizeof(new_rate)))
			return -EFAULT;

		if (new_rate < 1 || new_rate > VDAQ_MAX_RATE_HZ)
			return -EINVAL;

		new_period = ns_to_ktime(NSEC_PER_SEC / new_rate);

		mutex_lock(&dev->control_lock);

		/* Stop the current schedule before publishing the new period. */
		hrtimer_cancel(&dev->timer);
		WRITE_ONCE(dev->period, new_period);
		dev->sample_rate = new_rate;

		/* Changing the rate must not implicitly start a stopped device. */
		if (READ_ONCE(dev->running))
			hrtimer_start(&dev->timer, new_period, HRTIMER_MODE_REL);

		mutex_unlock(&dev->control_lock);
		break;
	}

	default:
		return -ENOTTY;
	}

	return 0;
}

/**
 * vdaq_ring_push - Generate metadata and append one sample to the ring buffer
 * @dev:    Virtual acquisition device
 * @sample: Partially initialized sample; timestamp and status are set by caller
 *
 * When the buffer is full, the oldest sample is overwritten. One slot is kept
 * unused so that head == tail unambiguously represents an empty buffer.
 */
static void vdaq_ring_push(struct vdaq_dev *dev, struct vdaq_sample *sample)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->data_lock, flags);

	sample->sequence = dev->sequence++;
	sample->channel[0] = sample->sequence;
	sample->channel[1] = sample->sequence * 2;
	sample->channel[2] = -(s16)sample->sequence;
	sample->channel[3] = sample->sequence % 100;
	dev->generated_samples++;

	dev->buffer[dev->head] = *sample;
	dev->head = (dev->head + 1) % VDAQ_BUFFER_SIZE;

	if (dev->head == dev->tail) {
		/* 缓冲区满，覆盖最旧的数据。 */
		dev->tail = (dev->tail + 1) % VDAQ_BUFFER_SIZE;
		dev->dropped_samples++;
		dev->buffer_overflows++;
	}

	spin_unlock_irqrestore(&dev->data_lock, flags);

	/* New data is available to blocking readers. */
	wake_up_interruptible(&dev->read_queue);
}

/**
 * vdaq_timer_callback - Periodically generate one virtual DAQ sample
 * @timer: hrtimer embedded in struct vdaq_dev
 *
 * Return: HRTIMER_RESTART while acquisition is running, otherwise
 * HRTIMER_NORESTART.
 */
static enum hrtimer_restart vdaq_timer_callback(struct hrtimer *timer)
{
	struct vdaq_dev *dev;
	struct vdaq_sample sample;
	ktime_t period;
	u32 sequence;
	u64 generated;
	u64 dropped;
	unsigned int head;
	unsigned int tail;
	unsigned long flags;
	bool print_status;

	/* 通过 hrtimer 成员地址反推出所属的 struct vdaq_dev 对象。 */
	dev = container_of(timer, struct vdaq_dev, timer);

	if (!READ_ONCE(dev->running))
		return HRTIMER_NORESTART;

	memset(&sample, 0, sizeof(sample));
	sample.timestamp_ns = ktime_get_ns();
	sample.status = 0; /* 状态正常。 */

	vdaq_ring_push(dev, &sample);

	/* Copy a coherent snapshot under the lock, then print outside it. */
	spin_lock_irqsave(&dev->data_lock, flags);
	sequence = dev->sequence;
	generated = dev->generated_samples;
	dropped = dev->dropped_samples;
	head = dev->head;
	tail = dev->tail;
	print_status = sequence % 100 == 0;
	spin_unlock_irqrestore(&dev->data_lock, flags);

	if (print_status)
		pr_info("vdaq: seq=%u generated=%llu dropped=%llu head=%u tail=%u\n",
			sequence, generated, dropped, head, tail);

	period = READ_ONCE(dev->period);
	hrtimer_forward_now(timer, period);

	return HRTIMER_RESTART;
}

static int __init vdaq_init(void)
{
	struct vdaq_dev *dev = &vdaq_device;
	int ret;

	/* 1. 初始化设备运行状态和同步原语。 */
	dev->head = 0;
	dev->tail = 0;
	dev->sequence = 0;
	dev->generated_samples = 0;
	dev->read_samples = 0;
	dev->dropped_samples = 0;
	dev->buffer_overflows = 0;
	dev->sample_rate = VDAQ_DEFAULT_RATE_HZ;
	dev->period = ns_to_ktime(VDAQ_DEFAULT_PERIOD_NS);
	dev->running = true;

	spin_lock_init(&dev->data_lock);
	mutex_init(&dev->control_lock);
	init_waitqueue_head(&dev->read_queue);

	/* 2. 注册字符设备并创建 /dev/vdaq0。 */
	ret = alloc_chrdev_region(&vdaq_chrdev.devno, 0, VDAQ_DEVICE_COUNT,
				  VDAQ_DEVICE_NAME);
	if (ret) {
		pr_err("vdaq: failed to allocate device number: %d\n", ret);
		return ret;
	}

	pr_info("vdaq: major=%u minor=%u\n",
		MAJOR(vdaq_chrdev.devno), MINOR(vdaq_chrdev.devno));

	cdev_init(&vdaq_chrdev.cdev, &vdaq_fops);
	vdaq_chrdev.cdev.owner = THIS_MODULE;

	ret = cdev_add(&vdaq_chrdev.cdev, vdaq_chrdev.devno,
		       VDAQ_DEVICE_COUNT);
	if (ret) {
		pr_err("vdaq: failed to add cdev: %d\n", ret);
		goto err_unregister_chrdev;
	}

	/* Linux 5.10 uses the two-argument class_create() form. */
	vdaq_chrdev.class = class_create(THIS_MODULE, VDAQ_DEVICE_NAME);
	if (IS_ERR(vdaq_chrdev.class)) {
		ret = PTR_ERR(vdaq_chrdev.class);
		pr_err("vdaq: failed to create class: %d\n", ret);
		goto err_del_cdev;
	}

	vdaq_chrdev.device = device_create(vdaq_chrdev.class, NULL,
					   vdaq_chrdev.devno, NULL,
					   VDAQ_DEVICE_NAME);
	if (IS_ERR(vdaq_chrdev.device)) {
		ret = PTR_ERR(vdaq_chrdev.device);
		pr_err("vdaq: failed to create device: %d\n", ret);
		goto err_destroy_class;
	}

	/* 3. 所有资源就绪后再初始化并启动 hrtimer。 */
	hrtimer_init(&dev->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	dev->timer.function = vdaq_timer_callback;
	hrtimer_start(&dev->timer, dev->period, HRTIMER_MODE_REL);

	pr_info("vdaq: module initialized, rate=%u Hz\n", dev->sample_rate);
	return 0;

err_destroy_class:
	class_destroy(vdaq_chrdev.class);
err_del_cdev:
	cdev_del(&vdaq_chrdev.cdev);
err_unregister_chrdev:
	unregister_chrdev_region(vdaq_chrdev.devno, VDAQ_DEVICE_COUNT);
	return ret;
}

static void __exit vdaq_exit(void)
{
	struct vdaq_dev *dev = &vdaq_device;

	pr_info("vdaq: removing module\n");

	/* 先停止数据源，确保后续不再访问即将释放的资源。 */
	WRITE_ONCE(dev->running, false);
	hrtimer_cancel(&dev->timer);
	wake_up_interruptible(&dev->read_queue);

	device_destroy(vdaq_chrdev.class, vdaq_chrdev.devno);
	class_destroy(vdaq_chrdev.class);
	cdev_del(&vdaq_chrdev.cdev);
	unregister_chrdev_region(vdaq_chrdev.devno, VDAQ_DEVICE_COUNT);

	pr_info("vdaq: module removed\n");
}

module_init(vdaq_init);
module_exit(vdaq_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Komorebi");
MODULE_DESCRIPTION("Virtual multi-channel data acquisition character driver");
MODULE_VERSION("0.3");