// SPDX-License-Identifier: GPL-2.0
#include "vdaq_internal.h"

#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/string.h>
#include <linux/uaccess.h>

static void vdaq_log_periodic_status(struct vdaq_device *dev)
{
	unsigned long flags;
	u64 generated;
	u64 dropped;
	u32 sequence;
	unsigned int head;
	unsigned int tail;

	spin_lock_irqsave(&dev->data_lock, flags);
	sequence = dev->sequence;
	generated = dev->generated_samples;
	dropped = dev->dropped_samples;
	head = dev->head;
	tail = dev->tail;
	spin_unlock_irqrestore(&dev->data_lock, flags);

	if (sequence % 100 == 0)
		pr_info("vdaq: seq=%u generated=%llu dropped=%llu head=%u tail=%u\n",
			sequence, generated, dropped, head, tail);
}

static enum hrtimer_restart vdaq_timer_callback(struct hrtimer *timer)
{
	struct vdaq_device *dev;
	struct vdaq_sample sample;

	dev = container_of(timer, struct vdaq_device, timer);
	if (!READ_ONCE(dev->running))
		return HRTIMER_NORESTART;

	memset(&sample, 0, sizeof(sample));
	sample.timestamp_ns = ktime_get_ns();
	vdaq_ring_push(dev, &sample);
	vdaq_log_periodic_status(dev);
	hrtimer_forward_now(timer, READ_ONCE(dev->period));
	return HRTIMER_RESTART;
}

bool vdaq_is_running(struct vdaq_device *dev) {
  return READ_ONCE(dev->running);
}

void vdaq_control_init(struct vdaq_device *dev)
{
	mutex_init(&dev->control_lock);
	dev->sample_rate = VDAQ_DEFAULT_RATE_HZ;
	dev->period = ns_to_ktime(VDAQ_DEFAULT_PERIOD_NS);
	dev->running = false;
	hrtimer_init(&dev->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	dev->timer.function = vdaq_timer_callback;
}

void vdaq_start(struct vdaq_device *dev)
{
	mutex_lock(&dev->control_lock);
	if (!READ_ONCE(dev->running)) {
		WRITE_ONCE(dev->running, true);
		hrtimer_start(&dev->timer, READ_ONCE(dev->period),
			      HRTIMER_MODE_REL);
	}
	mutex_unlock(&dev->control_lock);
}

void vdaq_stop(struct vdaq_device *dev)
{
	mutex_lock(&dev->control_lock);
	if (READ_ONCE(dev->running)) {
		WRITE_ONCE(dev->running, false);
		hrtimer_cancel(&dev->timer);
		wake_up_interruptible(&dev->read_queue);
	}
	mutex_unlock(&dev->control_lock);
}

void vdaq_control_shutdown(struct vdaq_device *dev)
{
	vdaq_stop(dev);
	hrtimer_cancel(&dev->timer);
}

int vdaq_set_rate(struct vdaq_device *dev, unsigned int rate)
{
	ktime_t period;

	if (rate < 1 || rate > VDAQ_MAX_RATE_HZ)
		return -EINVAL;
	period = ns_to_ktime(NSEC_PER_SEC / rate);

	mutex_lock(&dev->control_lock);
	hrtimer_cancel(&dev->timer);
	WRITE_ONCE(dev->period, period);
	dev->sample_rate = rate;
	if (READ_ONCE(dev->running))
		hrtimer_start(&dev->timer, period, HRTIMER_MODE_REL);
	mutex_unlock(&dev->control_lock);
	return 0;
}

int vdaq_get_rate(struct vdaq_device *dev, unsigned int *rate)
{
    if (!rate)
        return -EINVAL;

    mutex_lock(&dev->control_lock);
    *rate = dev->sample_rate;
    mutex_unlock(&dev->control_lock);
    return 0;
}

void vdaq_get_status(struct vdaq_device *dev, struct vdaq_stats *stats)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->data_lock, flags);
	stats->generated_samples = dev->generated_samples;
	stats->read_samples = dev->read_samples;
	stats->dropped_samples = dev->dropped_samples;
	stats->buffer_overflows = dev->buffer_overflows;
	stats->current_sequence = dev->sequence;
	stats->buffer_head = dev->head;
	stats->buffer_tail = dev->tail;
	spin_unlock_irqrestore(&dev->data_lock, flags);

	mutex_lock(&dev->control_lock);
	stats->rate = dev->sample_rate;
	stats->running = dev->running;
	mutex_unlock(&dev->control_lock);
}

static long vdaq_ioctl_get_status(struct vdaq_device *dev,
				  unsigned long arg)
{
	struct vdaq_stats stats;

	vdaq_get_status(dev, &stats);
	if (copy_to_user((void __user *)arg, &stats, sizeof(stats)))
		return -EFAULT;
	return 0;
}

static long vdaq_ioctl_set_rate(struct vdaq_device *dev, unsigned long arg)
{
	unsigned int rate;

	if (copy_from_user(&rate, (unsigned int __user *)arg, sizeof(rate)))
		return -EFAULT;
	return vdaq_set_rate(dev, rate);
}

long vdaq_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct vdaq_device *dev = file->private_data;

	switch (cmd) {
	case VDAQ_IOCTL_GET_STATUS:
		return vdaq_ioctl_get_status(dev, arg);
	case VDAQ_IOCTL_START:
		vdaq_start(dev);
		return 0;
	case VDAQ_IOCTL_STOP:
		vdaq_stop(dev);
		return 0;
	case VDAQ_IOCTL_CLEAR_BUFFER:
		vdaq_clear_buffer(dev);
		return 0;
	case VDAQ_IOCTL_SET_RATE:
		return vdaq_ioctl_set_rate(dev, arg);
	default:
		return -ENOTTY;
	}
}
