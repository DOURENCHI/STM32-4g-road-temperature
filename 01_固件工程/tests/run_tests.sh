#!/bin/sh
# ============================================================
#  应用层纯逻辑单元测试 — 一键编译并运行
#
#  用法（Git Bash / MSYS2 / Linux 均可）：
#      sh run_tests.sh
#
#  为什么要单独跑这一套测试：
#    固件里真正需要硬件的只有 GPIO 时序和串口收发；
#    数据帧、校验、缓存、重传这些逻辑完全不依赖硬件，
#    把它们抽出来单独测，才能在没有开发板的情况下
#    证明「这段代码是对的」，而不是「看起来是对的」。
# ============================================================
set -e
cd "$(dirname "$0")"

CC="${CC:-gcc}"
CFLAGS="-std=c99 -Wall -Wextra -O1"

echo "编译器: $($CC --version | head -1)"
echo "开始编译..."

$CC $CFLAGS -o test_app \
    ../App/app_dht_decode.c \
    ../App/app_protocol.c \
    ../App/app_ringbuf.c \
    ../App/app_retrans.c \
    test_dht_decode.c \
    test_protocol.c \
    test_ringbuf.c \
    test_retrans.c \
    test_integration.c \
    test_main.c

echo "编译通过（-Wall -Wextra 无警告）"
echo ""

./test_app
