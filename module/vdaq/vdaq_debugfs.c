#include "vdaq_internal.h"
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
/** stats */
static int vdaq_stats_show(struct seq_file *m, void *v) {
  struct vdaq_device *vdev = m->private;
  struct vdaq_stats snap = {0};
  if (!vdev) {
    return -ENODEV;
  }
  vdaq_get_status(vdev, &snap);
  seq_printf(m, "--- VDAQ Device Stats ---\n");
  seq_printf(m, "Running Status : %s\n", snap.running ? "RUNNING" : "STOPPED");
  seq_printf(m, "Sampling Rate  : %u Hz\n", snap.rate);
  seq_printf(m, "Generated Samples: %llu\n", snap.generated_samples);
  seq_printf(m, "Read Samples   : %llu\n", snap.read_samples);
  seq_printf(m, "Dropped Samples: %llu\n", snap.dropped_samples);
  seq_printf(m, "Buffer Overflows: %llu\n", snap.buffer_overflows);
  seq_printf(m, "Current Sequence: %u\n", snap.current_sequence);
  seq_printf(m, "Buffer Policy  : overwrite_oldest\n");
  return 0;
}

static int vdaq_stats_open(struct inode *inode, struct file *file) {
  return single_open(file, vdaq_stats_show, inode->i_private);
}

static const struct file_operations vdaq_stats_fops = {
    .owner = THIS_MODULE,
    .llseek = seq_lseek,
    .read = seq_read,
    .open = vdaq_stats_open,
    .release = single_release,
};

/** buffer */
static int vdaq_buffer_show(struct seq_file *m, void *v) {
  struct vdaq_device *vdev = m->private;
  struct vdaq_stats snap = {0};
  unsigned int used;
  
  if (!vdev) {
    return -ENODEV;
  }
  vdaq_get_status(vdev, &snap);
  used = (snap.buffer_head + VDAQ_BUFFER_SIZE - snap.buffer_tail) % VDAQ_BUFFER_SIZE;
  seq_printf(m, "--- VDAQ Device Buffer ---\n");
  seq_printf(m, "Buffer Capacity: %d\n", VDAQ_BUFFER_SIZE - 1);
  seq_printf(m, "Buffer Used    : %u\n", used);
  seq_printf(m, "Buffer Head    : %u\n", snap.buffer_head);
  seq_printf(m, "Buffer Tail    : %u\n", snap.buffer_tail);
  return 0;
}

static int vdaq_buffer_open(struct inode *inode, struct file *file) {
  return single_open(file, vdaq_buffer_show, inode->i_private);
}

static const struct file_operations vdaq_buffer_fops = {
    .owner = THIS_MODULE,
    .llseek = seq_lseek,
    .read = seq_read,
    .open = vdaq_buffer_open,
    .release = single_release,
};

int vdaq_debugfs_init(struct vdaq_device *dev) {
  int err;
  struct dentry *file;
  if (!dev)
    return -EINVAL;
  dev->debugfs_root = debugfs_create_dir(VDAQ_CLASS_NAME, NULL);
  if (IS_ERR_OR_NULL(dev->debugfs_root)) {
    err = dev->debugfs_root ? PTR_ERR(dev->debugfs_root) : -ENOMEM;

    dev->debugfs_root = NULL;
    return err;// 其他真实错误，如内存不足等，向上传递错误码
  }

  file = debugfs_create_file("stats", 0444, dev->debugfs_root, dev,
                      &vdaq_stats_fops);
  if (IS_ERR_OR_NULL(file)) {
    err = file ? PTR_ERR(file) : -ENOMEM;
    goto err_cleanup;
  }
  file = debugfs_create_file("buffer", 0444, dev->debugfs_root, dev,
                      &vdaq_buffer_fops);
  if (IS_ERR_OR_NULL(file)) {
    err = file ? PTR_ERR(file) : -ENOMEM;
    goto err_cleanup;
  }
  pr_info("vdaq_debugfs: registered successfully.\n");
  return 0;

err_cleanup:
  debugfs_remove_recursive(dev->debugfs_root);
  dev->debugfs_root = NULL;
  return err;
}

void vdaq_debugfs_exit(struct vdaq_device *dev) {
  if (dev->debugfs_root && !IS_ERR(dev->debugfs_root)) {
        debugfs_remove_recursive(dev->debugfs_root);
        dev->debugfs_root = NULL;
        pr_info("vdaq_debugfs: debugfs cleaned up successfully\n");
    }
}