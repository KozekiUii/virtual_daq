# 字符设备生命周期

## 初始化流程

alloc_chrdev_region

cdev_init

cdev_add

class_create

device_create

## 退出流程

device_destory

class_destory

cdev_del

unregister_chrdev_region

## 用户态调用链

open -> file_ops->vdaq_open

read -> file_ops->vdaq_read

close -> file_ops->release

## read的设计

第一次返回一帧，后续返回EOF

## 错误处理

说明如何回滚

## 当前不足

目前只返回固定数据，还没有定时器、缓冲区和阻塞读取