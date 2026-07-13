#!/usr/bin/env bash

set -uo pipefail

CTL="${CTL:-./vdaq_ctl}"
READER="${READER:-./reader}"

TEST1_LOG="/tmp/vdaq_test_empty_stop.log"
TEST2_LOG="/tmp/vdaq_test_blocked_stop.log"

PASS=0
FAIL=0

green() { printf '\033[32m%s\033[0m\n' "$*"; }
red()   { printf '\033[31m%s\033[0m\n' "$*"; }
yellow(){ printf '\033[33m%s\033[0m\n' "$*"; }

cleanup()
{
    yellow "恢复设备为 200 Hz 运行状态……"

    sudo "$CTL" stop >/dev/null 2>&1 || true
    sudo "$CTL" rate 200 >/dev/null 2>&1 || true
    sudo "$CTL" start >/dev/null 2>&1 || true
}

trap cleanup EXIT INT TERM

check_programs()
{
    if [[ ! -x "$CTL" ]]; then
        red "找不到可执行程序：$CTL"
        exit 1
    fi

    if [[ ! -x "$READER" ]]; then
        red "找不到可执行程序：$READER"
        exit 1
    fi

    if [[ ! -e /dev/vdaq0 ]]; then
        red "/dev/vdaq0 不存在，请先加载 vdaq.ko"
        exit 1
    fi
}

record_pass()
{
    green "[PASS] $1"
    PASS=$((PASS + 1))
}

record_fail()
{
    red "[FAIL] $1"
    FAIL=$((FAIL + 1))
}

test_stopped_empty_read()
{
    local rc

    yellow "测试 1：设备停止且缓冲区为空时，read 是否立即返回"

    sudo "$CTL" stop
    sudo "$CTL" clear

    : > "$TEST1_LOG"

    set +e
    timeout 3 sudo "$READER" >"$TEST1_LOG" 2>&1
    rc=$?
    set -e

    if [[ $rc -eq 124 ]]; then
        record_fail "reader 超时，说明停止状态下仍永久阻塞"
    else
        record_pass "reader 没有永久阻塞，退出码为 $rc"
    fi

    echo "reader 输出："
    cat "$TEST1_LOG"
    echo
}

test_blocked_reader_wakeup()
{
    local reader_pid
    local exited=0
    local i

    yellow "测试 2：阻塞中的 reader 能否被 STOP 唤醒"

    sudo "$CTL" stop
    sudo "$CTL" rate 1
    sudo "$CTL" clear
    sudo "$CTL" start

    : > "$TEST2_LOG"

    sudo "$READER" >"$TEST2_LOG" 2>&1 &
    reader_pid=$!

    # 1 Hz 的第一次采样约在 1 秒后发生。
    # 在 0.2 秒时停止，reader 此时通常仍阻塞在 read()。
    sleep 0.2
    sudo "$CTL" stop

    # 最多等待约 3 秒，检查 reader 是否退出。
    for i in $(seq 1 30); do
        if ! kill -0 "$reader_pid" 2>/dev/null; then
            exited=1
            break
        fi
        sleep 0.1
    done

    if [[ $exited -eq 1 ]]; then
        wait "$reader_pid" 2>/dev/null || true
        record_pass "STOP 成功唤醒阻塞中的 reader"
    else
        record_fail "STOP 后 reader 仍未退出"

        sudo kill "$reader_pid" 2>/dev/null || true
        wait "$reader_pid" 2>/dev/null || true
    fi

    echo "reader 输出："
    cat "$TEST2_LOG"
    echo
}

main()
{
    check_programs

    # 提前完成 sudo 验证，避免后台测试时出现密码提示。
    sudo -v

    test_stopped_empty_read
    test_blocked_reader_wakeup

    echo "=============================="
    echo "测试通过：$PASS"
    echo "测试失败：$FAIL"
    echo "=============================="

    if [[ $FAIL -ne 0 ]]; then
        exit 1
    fi

    green "全部测试通过"
}

main "$@"