#!/usr/bin/env bash
# StickS3 x OpenCode — ESP-IDF 环境包装脚本
#
# 背景（2026-09-20 实测）：
#   1) ESP-IDF 的 Python venv 按「检测到的 python3 版本」命名。本机系统 python3 是 3.14，
#      过新，导致 venv 建不出来 / export.sh 静默失败（返回 0 但 idf.py 不在 PATH）。
#   2) openocd-esp32 依赖系统库 libusb-1.0.so.0。本机未安装该库且无免密 sudo，
#      故用用户态解包的方式提供（见下方 STICKS3_LIBUSB_DIR）。
#
# 用法（必须 source，不要执行）：
#   source tools/idf-env.sh
#   idf.py build

STICKS3_IDF_PATH="${STICKS3_IDF_PATH:-$HOME/esp/esp-idf}"
STICKS3_PY_SHIM_DIR="${STICKS3_PY_SHIM_DIR:-$HOME/.local/share/sticks3-esp-bin}"
STICKS3_LIBUSB_DIR="${STICKS3_LIBUSB_DIR:-$HOME/.local/share/sticks3-esp-libs/usr/lib/x86_64-linux-gnu}"

if [ ! -f "$STICKS3_IDF_PATH/export.sh" ]; then
    echo "ERROR: 找不到 ESP-IDF export.sh：$STICKS3_IDF_PATH/export.sh" >&2
    echo "可用 STICKS3_IDF_PATH 指定 ESP-IDF 路径。" >&2
    return 1 2>/dev/null || exit 1
fi

# 持久化的 python3 -> 3.12 软链接（/tmp 会被清理，故不放 /tmp）
if [ ! -x "$STICKS3_PY_SHIM_DIR/python3" ]; then
    mkdir -p "$STICKS3_PY_SHIM_DIR" || return 1 2>/dev/null
    ln -sf "$(command -v python3.12)" "$STICKS3_PY_SHIM_DIR/python3" || return 1 2>/dev/null
fi

export PATH="$STICKS3_PY_SHIM_DIR:$PATH"

# 用户态 libusb：让 openocd-esp32 能通过 ESP-IDF 的工具校验
if [ -e "$STICKS3_LIBUSB_DIR/libusb-1.0.so.0" ]; then
    export LD_LIBRARY_PATH="$STICKS3_LIBUSB_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
else
    echo "WARNING: 未找到用户态 libusb（$STICKS3_LIBUSB_DIR）。" >&2
    echo "         openocd 校验会失败，export.sh 可能无法激活。参见 README「已知限制」。" >&2
fi

# 显式指向 3.12 venv，避免 export.sh 按错误版本重新推断
if [ -d "$HOME/.espressif/python_env/idf5.5_py3.12_env" ]; then
    export IDF_PYTHON_ENV_PATH="$HOME/.espressif/python_env/idf5.5_py3.12_env"
fi

# shellcheck disable=SC1090
. "$STICKS3_IDF_PATH/export.sh"
