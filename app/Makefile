CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -g -I../include

# 1. 声明所有的目标名称
TARGET_NAMES := reader test vdaq_ctl vdaq_monitor nonblock_reader vdaq_epoll_monitor vdaq_epoll_et_monitor

# 2. 自动给目标加上 build/ 前缀
# 例如：reader -> build/reader
TARGETS      := $(addprefix build/, $(TARGET_NAMES))

.PHONY: all clean run

# 默认编译所有位于 build 目录下的目标
all: $(TARGETS)

# 3. 核心模式匹配规则：
# 每一个 build/下的目标，都依赖于同名的 .c 文件
# 同时使用 | build 确保编译前 build 文件夹已创建
build/%: %.c | build
	$(CC) $(CFLAGS) -o $@ $<

# 4. 如果 build 目录不存在则创建它
build:
	mkdir -p build

# 运行规则（默认运行列表里的第一个，或者你可以指定运行某一个）
run: $(TARGETS)
	./build/vdaq_monitor

# 清理规则：直接连根拔除 build 目录
clean:
	rm -rf build