# vDAQ README Refresh Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 README 更新为准确反映阶段 4 与模块化架构的工程首页。

**Architecture:** README 提供项目定位、能力、架构、快速上手、接口、验证与 Roadmap；深入实现通过链接路由到 `docs/`。所有命令和路径以当前 LubanCat 仓库为准。

**Tech Stack:** Markdown、Linux 5.10、kernel module、GNU Make

---

### Task 1: 重写工程首页

**Files:**
- Modify: `README.md`

- [x] 更新项目定位与已完成功能，删除过期 Future 描述。
- [x] 增加模块化架构、数据流和关键文件职责。
- [x] 增加真实构建、加载、控制、read、poll 和 epoll 命令。
- [x] 记录 Ring Buffer 有效容量、锁边界与事件语义。
- [x] 写入已执行验证和阶段 5～10 Roadmap。
- [x] 链接现有 `docs/` 文档。

### Task 2: 准确性与格式验证

**Files:**
- Test: `README.md`

- [x] 对照 Makefile、CLI usage、UAPI 和目录树检查所有名称。
- [x] 检查 Markdown 围栏、表格和相对链接。
- [x] 运行 `git diff --check`。
- [x] 确认 diff 只包含 README 与本计划状态，不包含超声波笔记。
- [x] 创建提交：`docs: refresh project README`。
