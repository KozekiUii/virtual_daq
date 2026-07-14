// SPDX-License-Identifier: GPL-2.0
#include "vdaq_internal.h"

#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/uaccess.h>

static bool vdaq_ring_empty(const struct vdaq_device *dev)
{
	return READ_ONCE(dev->head) == READ_ONCE(dev->tail);
}

void vdaq_buffer_init(struct vdaq_device *dev)
{
	dev->head = 0;
	dev->tail = 0;
	dev->sequence = 0;
	dev->generated_samples = 0;
	dev->read_samples = 0;
	dev->dropped_samples = 0;
	dev->buffer_overflows = 0;
	spin_lock_init(&dev->data_lock);
	init_waitqueue_head(&dev->read_queue);
}

void vdaq_clear_buffer(struct vdaq_device *dev)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->data_lock, flags);
	dev->head = 0;
	dev->tail = 0;
	spin_unlock_irqrestore(&dev->data_lock, flags);
}

static int vdaq_ring_pop(struct vdaq_device *dev,
			 struct vdaq_sample *sample)
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
	if (dev->head == dev->tail && !READ_ONCE(dev->running))
		notify_hup = true;
	spin_unlock_irqrestore(&dev->data_lock, flags);

	if (notify_hup)
		wake_up_interruptible_poll(&dev->read_queue,
					   EPOLLIN | EPOLLRDNORM);
	return 0;
}

void vdaq_ring_push(struct vdaq_device *dev, struct vdaq_sample *sample)
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
		dev->tail = (dev->tail + 1) % VDAQ_BUFFER_SIZE;
		dev->dropped_samples++;
		dev->buffer_overflows++;
	}
	spin_unlock_irqrestore(&dev->data_lock, flags);
	wake_up_interruptible(&dev->read_queue);
}

static int vdaq_wait_for_sample(struct file *file, struct vdaq_device *dev)
{
	int ret;

	if (!vdaq_ring_empty(dev))
		return 0;
	if (!READ_ONCE(dev->running) || (file->f_flags & O_NONBLOCK))
		return -EAGAIN;

	ret = wait_event_interruptible(dev->read_queue,
				       !vdaq_ring_empty(dev) ||
				       !READ_ONCE(dev->running));
	if (ret)
		return ret;
	if (vdaq_ring_empty(dev) && !READ_ONCE(dev->running))
		return -EAGAIN;
	return 0;
}

ssize_t vdaq_read(struct file *file, char __user *buf, size_t count,
		  loff_t *ppos)
{
	struct vdaq_device *dev = file->private_data;
	struct vdaq_sample sample;
	int ret;

	(void)ppos;
	if (count < sizeof(sample))
		return -EINVAL;

	ret = vdaq_wait_for_sample(file, dev);
	if (ret)
		return ret;
	if (vdaq_ring_pop(dev, &sample))
		return -EAGAIN;
	if (copy_to_user(buf, &sample, sizeof(sample)))
		return -EFAULT;
	return sizeof(sample);
}

__poll_t vdaq_poll(struct file *file, struct poll_table_struct *wait)
{
	struct vdaq_device *dev = file->private_data;
	unsigned long flags;
	__poll_t mask = 0;
	bool readable;
	bool running;

	poll_wait(file, &dev->read_queue, wait);
	spin_lock_irqsave(&dev->data_lock, flags);
	readable = !vdaq_ring_empty(dev);
	running = READ_ONCE(dev->running);
	spin_unlock_irqrestore(&dev->data_lock, flags);

	if (readable)
		mask |= EPOLLIN | EPOLLRDNORM;
	else if (!running)
		mask |= EPOLLHUP;
	return mask;
}
