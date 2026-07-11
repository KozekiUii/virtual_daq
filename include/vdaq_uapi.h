#ifndef _VDAQ_UAPI_H_
#define _VDAQ_UAPI_H_

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/ioctl.h>
#else
#include <stdint.h>
#include <sys/ioctl.h>
#endif

#define VDAQ_IOCTL_MAGIC 'v'
#define VDAQ_IOCTL_GET_STATUS _IOR(VDAQ_IOCTL_MAGIC, 1, struct vdaq_stats)
#define VDAQ_IOCTL_START _IO(VDAQ_IOCTL_MAGIC, 2)
#define VDAQ_IOCTL_STOP _IO(VDAQ_IOCTL_MAGIC, 3)
#define VDAQ_IOCTL_CLEAR_BUFFER _IO(VDAQ_IOCTL_MAGIC, 4)
#define VDAQ_IOCTL_SET_RATE _IOW(VDAQ_IOCTL_MAGIC, 5, unsigned int)

struct vdaq_sample {
#ifdef __KERNEL__
    __u64 timestamp_ns;
    __u32 sequence;
    __s16 channel[4];
    __u16 status;
#else
    uint64_t timestamp_ns;
    uint32_t sequence;
    int16_t channel[4];
    uint16_t status;
#endif
};

struct vdaq_stats {
#ifdef __KERNEL__
    __u64 generated_samples;
    __u64 read_samples;
    __u64 dropped_samples;
    __u64 buffer_overflows;
    __u32 current_sequence;
    __u32 buffer_head;
    __u32 buffer_tail;
    __u32 running; // 1表示正在运行，0表示已停止
#else
    uint64_t generated_samples;
    uint64_t read_samples;
    uint64_t dropped_samples;
    uint64_t buffer_overflows;
    uint32_t current_sequence;
    uint32_t buffer_head;
    uint32_t buffer_tail;
    uint32_t running; // 1表示正在运行，0表示已停止
#endif
};

#endif