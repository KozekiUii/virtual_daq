# vDAQ Modularization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将单文件 vDAQ 驱动拆成职责清晰的复合内核模块，同时保持阶段 4 的全部外部行为不变。

**Architecture:** 一个 `struct vdaq_device` 统一保存字符设备资源和运行状态，`open()` 将设备写入 `file->private_data`。数据路径、控制路径与模块生命周期分别由 buffer、control、main 文件负责，内部接口集中在 `vdaq_internal.h`。

**Tech Stack:** Linux 5.10 kernel module、C、hrtimer、cdev、wait queue、poll/epoll、GNU Make

---

### Task 1: 建立模块化构建骨架

**Files:**
- Create: `module/vdaq/vdaq_internal.h`
- Modify: `module/vdaq/Makefile`

- [x] 定义统一设备结构、常量和内部函数声明，不暴露新的 UAPI。
- [x] 将 Makefile 改为复合目标：`vdaq-y := vdaq_main.o vdaq_buffer.o vdaq_control.o`。
- [x] 运行模块构建并确认三个对象参与复合链接。

### Task 2: 拆分数据路径

**Files:**
- Create: `module/vdaq/vdaq_buffer.c`
- Source: `module/vdaq/vdaq.c`

- [x] 迁移 Ring Buffer 初始化、判空、push、pop。
- [x] 迁移 `read()` 和 `poll()`，改为从 `file->private_data` 获取设备。
- [x] 保持阻塞、非阻塞、STOP/HUP 和覆盖最旧数据语义不变。
- [x] 编译对象文件，无 warning。

### Task 3: 拆分控制路径

**Files:**
- Create: `module/vdaq/vdaq_control.c`
- Source: `module/vdaq/vdaq.c`

- [x] 迁移 hrtimer 初始化、回调和退出同步。
- [x] 抽取 `vdaq_start()`、`vdaq_stop()`、`vdaq_set_rate()`、`vdaq_get_status()` 和 `vdaq_clear_buffer()`。
- [x] 让 ioctl 只负责命令分派及用户空间复制，为阶段 5 sysfs 复用 helper 做准备。
- [x] 编译对象文件，无 warning。

### Task 4: 拆分模块生命周期

**Files:**
- Create: `module/vdaq/vdaq_main.c`
- Delete: `module/vdaq/vdaq.c`

- [x] 迁移字符设备注册、失败回滚和模块退出。
- [x] 在 `open()` 中通过 `inode->i_cdev` 获取统一设备对象并设置 `file->private_data`。
- [x] 注册资源成功后启动采样；退出时先停数据源再逆序释放字符设备资源。
- [x] 完整构建 `vdaq.ko`，无 warning。

### Task 5: 阶段 4 回归验证

**Files:**
- Test: `app/*`
- Test: `module/vdaq/test10.sh`

- [x] 构建全部应用程序。
- [x] 加载模块并验证 `/dev/vdaq0`。
- [x] 验证 GET_STATUS、START、STOP、SET_RATE、CLEAR_BUFFER。
- [x] 验证 100 Hz 连续读取和 1 Hz 阻塞/非阻塞差异。
- [x] 验证 poll、epoll、stdin 退出及 STOP/HUP。
- [x] 卸载模块并检查内核日志无 warning、oops 或资源残留。
- [x] 检查所有新增源文件 ≤300 行、函数 ≤50 行。

### Task 6: 交付收口

**Files:**
- Update: `docs/superpowers/plans/2026-07-14-vdaq-modularization.md`

- [x] 将实际验证结果写入计划状态。
- [x] 审查 diff，确认未修改 UAPI、应用层和超声波笔记。
- [x] 创建独立提交：`refactor: modularize vdaq driver`。

## Verification Results

- 内核模块由 `vdaq_main.o`、`vdaq_buffer.o`、`vdaq_control.o` 成功链接，无编译 warning。
- checkpatch：4 个新增文件 0 error、0 warning、0 check。
- 1 Hz 非阻塞读取立即返回，阻塞读取约 0.999～1.002 秒后返回。
- poll、epoll、stdin 退出和 STOP/HUP 测试通过；strace 观察到阻塞于 `epoll_pwait()`。
- STOP 唤醒测试 2/2 通过，加载—读取—卸载循环 50/50 通过。
- 最近内核日志未发现 warning、oops、use-after-free 或 vDAQ 错误。
- sparse 未安装，因此 `make C=1 CHECK=sparse` 无法执行；普通内核构建和 checkpatch 已通过。
