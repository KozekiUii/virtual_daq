/* SPDX-License-Identifier: GPL-2.0 */
#ifndef VDAQ_INTERNAL_H
#define VDAQ_INTERNAL_H

#include "vdaq_uapi.h"

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/hrtimer.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/wait.h>

#define VDAQ_CLASS_NAME "vdaq"
#define VDAQ_DEVICE_NAME "vdaq0"
#define VDAQ_DEVICE_COUNT 1
#define VDAQ_BUFFER_SIZE 1024
#define VDAQ_DEFAULT_RATE_HZ 100U
#define VDAQ_MAX_RATE_HZ 10000U
#define VDAQ_DEFAULT_PERIOD_NS (NSEC_PER_SEC / VDAQ_DEFAULT_RATE_HZ)

struct vdaq_device {
	dev_t devno;
	struct cdev cdev;
	struct class *class;
	struct device *device;
	struct dentry *debugfs_root;
        
	struct vdaq_sample buffer[VDAQ_BUFFER_SIZE];
	unsigned int head;
	unsigned int tail;
	/* Protects the ring buffer and data-path counters. */
	spinlock_t data_lock;
	wait_queue_head_t read_queue;

	/* Serializes timer lifecycle and sampling-rate changes. */
	struct mutex control_lock;
	struct hrtimer timer;
	ktime_t period;
	unsigned int sample_rate;
	bool running;

	u32 sequence;
	u64 generated_samples;
	u64 dropped_samples;
	u64 buffer_overflows;
	u64 read_samples;
};

void vdaq_buffer_init(struct vdaq_device *dev);
void vdaq_ring_push(struct vdaq_device *dev, struct vdaq_sample *sample);
void vdaq_clear_buffer(struct vdaq_device *dev);
ssize_t vdaq_read(struct file *file, char __user *buf, size_t count,
		  loff_t *ppos);
__poll_t vdaq_poll(struct file *file, struct poll_table_struct *wait);

bool vdaq_is_running(struct vdaq_device *dev);
void vdaq_control_init(struct vdaq_device *dev);
void vdaq_control_shutdown(struct vdaq_device *dev);
void vdaq_start(struct vdaq_device *dev);
void vdaq_stop(struct vdaq_device *dev);
int vdaq_set_rate(struct vdaq_device *dev, unsigned int rate);
int vdaq_get_rate(struct vdaq_device *dev, unsigned int *rate);
void vdaq_get_status(struct vdaq_device *dev, struct vdaq_stats *stats);
long vdaq_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

int vdaq_sysfs_register(struct vdaq_device *dev);
void vdaq_sysfs_unregister(struct vdaq_device *dev);

int vdaq_debugfs_init(struct vdaq_device *dev);
void vdaq_debugfs_exit(struct vdaq_device *dev);
#endif
