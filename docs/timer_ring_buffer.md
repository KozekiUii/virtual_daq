# hrtimer 与环形缓冲设计

## 目标
使用hrtimer模拟100Hz采集设备

## 数据流
hrtimer -> sample -> ring buffer -> read -> user

## hrtimer 设计
周期为10ms，通过hrtimer_forword_now() 实现周期触发

## ring buffer 设计
head 表示下一次写入位置
tail 表示下一次读取位置
head == tail表示空
（head + 1）% size == tail 表示满

## 满缓冲策略
覆盖旧数据，tial前移，dropped_samples 增加

## 锁设计
使用spinlock保护head、tail、buf
不在spinlock内执行copy_to_user

## read 设计
read 在buffer为空时，通过wait_event_interruptible 阻塞
push 数据后 wake_up_interruptible 唤醒

## 当前不足
暂不支持 O_NOBLOCK、poll、ioctl 和运行时修改采样率