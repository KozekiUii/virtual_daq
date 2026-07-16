#include "vdaq_internal.h"
#include <linux/kernel.h>
#include <linux/sysfs.h>
#include <linux/string.h>

/*enable 读写*/
static ssize_t vdaq_enable_show(struct device *dev,
                                struct device_attribute *attr, char *buf) {
  struct vdaq_device *vdev;
  // dev_get_drvdata(const struct device * dev)从内核标准的struct device结构体中，安全地取出程序员之前绑定进去的自定义私有数据指针
	vdev = dev_get_drvdata(dev);
	if (!vdev)
		return -ENODEV;

	return sysfs_emit(buf, "%u\n",
			  vdaq_is_running(vdev) ? 1 : 0);
}
static ssize_t vdaq_enable_store(struct device *dev,
                                 struct device_attribute *attr,
                                 const char *buf, size_t count) {
	struct vdaq_device *vdev;
	bool enable;
	int ret;

	vdev = dev_get_drvdata(dev);
	if (!vdev)
		return -ENODEV;

	ret = kstrtobool(buf, &enable);
	if (ret)
		return ret;

	if (enable)
		vdaq_start(vdev);
	else
		vdaq_stop(vdev);

	return count;

}
static DEVICE_ATTR_RW(vdaq_enable);

/*sampling_rate_hz 读写*/
static ssize_t vdaq_sampling_rate_hz_show(struct device *dev, struct device_attribute *attr, char *buf) {
  struct vdaq_device *vdev = dev_get_drvdata(dev);
  unsigned int rate;
  int ret;
  if (!vdev) {
    return -ENODEV;
  }
  ret = vdaq_get_rate(vdev, &rate);
  if (ret) {
    return ret;
  }
  return sysfs_emit(buf, "%u\n", rate);
}
static ssize_t vdaq_sampling_rate_hz_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {
  struct vdaq_device *vdev = dev_get_drvdata(dev);
  unsigned int rate;
  int ret;
  if (!vdev) {
    return -ENODEV;
  }
  ret = kstrtouint(buf, 10, &rate);
  if (ret < 0)
    return ret;
  ret = vdaq_set_rate(vdev, rate);
  if (ret < 0)
    return ret;

  return count;
}
static DEVICE_ATTR_RW(vdaq_sampling_rate_hz);

/*state 只读*/
static ssize_t vdaq_state_show(struct device *dev, struct device_attribute *attr, char *buf) {
	struct vdaq_device *vdev;

	vdev = dev_get_drvdata(dev);
	if (!vdev)
		return -ENODEV;

	return sysfs_emit(buf, "%s\n",
			  vdaq_is_running(vdev) ? "running" : "stopped");
}
static DEVICE_ATTR_RO(vdaq_state);

/*buffer_capacity 只读*/
static ssize_t vdaq_buffer_capacity_show(struct device *dev, struct device_attribute *attr, char *buf) {
  return sysfs_emit(buf, "%u\n", VDAQ_BUFFER_SIZE - 1);
}
static DEVICE_ATTR_RO(vdaq_buffer_capacity);

/*buffer_policy 只读*/
static ssize_t vdaq_buffer_policy_show(struct device *dev, struct device_attribute *attr, char *buf) {
  return sysfs_emit(buf, "%s\n", "overwrite_oldest");
}
static DEVICE_ATTR_RO(vdaq_buffer_policy);

/* 2. 构造属性组 */
static struct attribute *vdaq_attrs[] = {
    &dev_attr_vdaq_enable.attr, &dev_attr_vdaq_sampling_rate_hz.attr,
    &dev_attr_vdaq_state.attr, &dev_attr_vdaq_buffer_capacity.attr,
    &dev_attr_vdaq_buffer_policy.attr,
    NULL,
};

static const struct attribute_group vdaq_attr_group = {
    .attrs = vdaq_attrs,
};

/**
 * vdaq_sysfs_register - 动态为已有设备注册 sysfs 属性组
 * @dev: 传入由 vdaq_main.c 创建并管理生命周期的 device 指针
 * @return：0 表示成功，负数表示失败
 */
int vdaq_sysfs_register(struct vdaq_device *dev) {
  int ret;
  if (!dev) {
    return -EINVAL;
  }

  ret = sysfs_create_group(&dev->device->kobj, &vdaq_attr_group);
  if (ret) {
    pr_err("vdaq_sysfs: failed to create sysfs group, err=%d\n", ret);
    return ret;
  }
  return 0;
}

/**
 * vdaq_sysfs_unregister - 动态注销 sysfs 属性组
 * @dev: 传入设备指针
 */
void vdaq_sysfs_unregister(struct vdaq_device *dev) {
  if (dev) {
    sysfs_remove_group(&dev->device->kobj, &vdaq_attr_group);
  }
}