# Virtual DAQ

基于 Linux Kernel 的虚拟多通道实时数据采集设备驱动。

本项目模拟工业数据采集设备的数据产生、缓存、传输和设备控制流程，
通过 Linux 内核模块实现虚拟采集设备 `/dev/vdaq0`。

---

## Features

- Linux 字符设备驱动模型
- hrtimer 实时周期采样
- 多通道数据模拟生成
- Ring Buffer 数据缓存
- 用户态/内核态数据传输
- ioctl 设备控制接口
- 动态采样率调整
- 阻塞 read 与唤醒机制
- 并发安全设计


---

## Architecture
             User Space

    +----------------------+
    |   reader/vdaq_ctl    |
    +----------+-----------+
               |
          ioctl/read
               |
    +----------v-----------+
    |    vdaq driver       |
    +----------------------+
               |
    +----------+-----------+
    |                      |
    Control path          Data path
    |                      |
    mutex                 spinlock
    |                      |
    +----------v-----------+
    START/STOP/RATE Ring Buffer
    +----------v-----------+
            hrtimer
                |
        sample generator

---

## Driver Design

### Data Flow
        hrtimer callback
            |
            v
        generate sample
            |
            v
        Ring Buffer
            |
            v
        read()
            |
            v
        user application


---

## Sampling

defaults：
- sample rate: 100Hz
- channels: 4
- buffer size: 1024 samples


sample format:

```c
struct vdaq_sample {

    uint64_t timestamp_ns;

    uint32_t sequence;

    int16_t channel[4];

    uint16_t status;

};
```

## Ring Buffer

采用循环队列：

- head:
producer position

- tail:
consumer position


当缓冲区满：
- new data overwrite old data
- dropped_samples++
- buffer_overflows++

## Synchronization
### Data path
hrtimer callback 运行在内核上下文：
 使用spinlock保护：
- ring buffer
- sequence
- statistics

### Control path
用户 ioctl：
使用mutex保护：
- start
- stop
- set rate

## IOCTL
支持：
| Command      | Description |
| ------------ | ----------- |
| GET_STATUS   | 获取运行状态      |
| START        | 启动采样        |
| STOP         | 停止采样        |
| CLEAR_BUFFER | 清空缓存        |
| SET_RATE     | 修改采样率       |

## Test
### Build

```make```

### Load

```sudo insmod vdaq.ko```

### Check

```dmesg -w```

### Control

#### 启动：

```./vdaq_ctl start```

#### 修改采样率：

```./vdaq_ctl rate 200```

#### 查看状态：

```./vdaq_ctl status```

#### 停止：

```./vdaq_ctl stop```

## Verification

### 已验证：

- 100Hz稳定采样
- 200Hz动态调频
- Ring Buffer覆盖策略
- dropped_samples统计
- STOP唤醒阻塞reader
- 模块安全加载卸载
# Future
- poll/select/epoll支持
- sysfs设备参数导出
- DMA模拟
- 故障注入与自动恢复