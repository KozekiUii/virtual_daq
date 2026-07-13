# vDAQ 驱动模块化设计

## 目标

在不改变现有 UAPI、设备节点、采样数据、锁语义和用户态行为的前提下，
将单文件 `vdaq.c` 拆成职责清晰的复合内核模块，为阶段 5 的 sysfs 与
debugfs 接口提供稳定边界。

## 学习分工

- 驱动层核心代码由学习者自行设计和编写；助手提供原理、接口约束、设计方案、
  代码审查、问题定位与验收。
- 应用层和测试脚本可由助手提供完整参考实现，但必须讲解系统调用、数据流、
  错误处理和驱动交互方式。
- 本次模块化属于结构性重构，由助手实施；阶段 5 新增的 sysfs/debugfs 驱动代码
  仍由学习者按照后续方案自行完成。
- HC-SR04 真实硬件接入暂缓。未完成的 HC-SR04 字段不保留在活动结构中，后续以
  LubanCat-5 V2、GPIO 描述符、Echo 双边沿 IRQ 和安全电平转换为独立阶段设计。

## 模块边界

```text
module/vdaq/
├── vdaq_internal.h   内部常量、设备结构和跨文件声明
├── vdaq_main.c       模块生命周期、字符设备注册、open/release
├── vdaq_buffer.c     Ring Buffer、read、poll、等待队列
├── vdaq_control.c    hrtimer、控制 helper、ioctl、状态快照
└── Makefile          复合模块构建规则
```

阶段 5 在上述基线通过后新增：

```text
├── vdaq_sysfs.c      正式配置属性
└── vdaq_debugfs.c    调试状态和统计接口
```

## 核心对象与数据流

使用统一的 `struct vdaq_device` 保存字符设备注册资源和运行时状态。模块只定义一个
设备实例，但文件操作不直接引用全局实例：`open()` 通过 `inode->i_cdev` 找到设备，
并写入 `file->private_data`，之后 `read()`、`poll()` 和 `ioctl()` 从该指针取得设备。

数据路径保持不变：hrtimer 生成采样，Ring Buffer 保存采样，等待队列通知阻塞读取和
poll/epoll。`data_lock` 保护缓冲区、计数器和数据路径状态；`control_lock` 串行化
START、STOP 与 SET_RATE。

控制逻辑抽成内部 helper，ioctl 只负责用户参数复制和命令分派。阶段 5 的 sysfs 将
复用相同 helper，避免形成两套计时器生命周期逻辑。

## 行为与错误处理

- `/dev/vdaq0`、ioctl 编号和 `struct vdaq_sample`、`struct vdaq_stats` 保持不变。
- 阻塞 read、`O_NONBLOCK`、STOP 唤醒、poll/epoll 的现有语义保持不变。
- 初始化按资源申请顺序执行，失败路径严格逆序释放；退出时先停止 hrtimer 和唤醒
  等待者，再移除设备节点、class、cdev 和设备号。
- 跨文件 helper 仅在模块内部可见，不使用 `EXPORT_SYMBOL`。
- 每个源文件不超过 300 行，函数不超过 50 行，避免新增魔法数字。

## 构建方案

输出模块名继续为 `vdaq.ko`：

```makefile
obj-m += vdaq.o
vdaq-y := vdaq_main.o vdaq_buffer.o vdaq_control.o
```

原 `vdaq.c` 被拆分后删除，避免与复合目标 `vdaq.o` 冲突。

## 验收

1. 内核模块和全部应用程序以 `-Wall -Wextra` 无新增 warning 编译。
2. 模块可加载、卸载，`/dev/vdaq0` 正常创建和删除。
3. START、STOP、SET_RATE、GET_STATUS、CLEAR_BUFFER 行为保持不变。
4. 100 Hz 连续读取、1 Hz 阻塞/非阻塞对比、poll、epoll、stdin 退出和 STOP/HUP
   回归通过。
5. 反复加载卸载无 warning、oops、use-after-free 或资源残留。
6. 模块化重构独立提交，阶段 5 功能不混入该提交。

