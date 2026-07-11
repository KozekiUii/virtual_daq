# Driver Design

## Character Device

Uses alloc_chrdev_region, cdev_init, cdev_add and device_create.

Device node:

    /dev/vdaq0

## Sample Structure

``` c
struct vdaq_sample {
    uint64_t timestamp_ns;
    uint32_t sequence;
    int16_t channel[4];
    uint16_t status;
};
```

## Hrtimer Sampling

Default sampling frequency: 100Hz.

Flow:

    hrtimer callback
     -> generate sample
     -> push buffer
     -> wake reader

## Ring Buffer

Supports FIFO, overwrite-oldest policy and dropped sample statistics.

## Concurrency

spinlock protects data path. mutex protects control path.
