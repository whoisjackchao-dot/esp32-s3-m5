#!/usr/bin/env bash
# StickS3 x OpenCode — ESP-IDF 环境包装脚本（WSL）
#
# 背景（2026-09-25 新机迁移后重建）：
#   1) ESP-IDF v5.5.4 位于 $HOME/esp/esp-idf；其 Python venv 由系统 python3.14 创建，
#      目录名 idf5.5_py3.14_env（ESP-IDF v5.5.4 已支持 Python 3.14）。
#   2) openocd-esp32 依赖系统库 libusb-1.0.so.0。本机无免密 sudo，故用用户态
#      解包方式提供（见下方 STICKS3_LIBUSB_DIR）。
#
# 用法（必须 source，不要执行）：
#   source tools/idf-env.sh
#   idf.py build

STICKS3_IDF_PATH="${STICKS3_IDF_PATH:-$HOME/esp/esp-idf}"
STICKS3_LIBUSB_DIR="${STICKS3_LIBUSB_DIR:-$HOME/.local/share/sticks3-esp-libs/usr/lib/x86_64-linux-gnu}"

if [ ! -f "$STICKS3_IDF_PATH/export.sh" ]; then
    echo "ERROR: 找不到 ESP-IDF export.sh：$STICKS3_IDF_PATH/export.sh" >&2
    echo "可用 STICKS3_IDF_PATH 指定 ESP-IDF 路径。" >&2
    return 1 2>/dev/null || exit 1
fi

# 自动探测 ESP-IDF python venv（idf5.5_py*_env），避免硬编码 Python 小版本
if [ -z "${IDF_PYTHON_ENV_PATH:-}" ]; then
    for d in "$HOME"/.espressif/python_env/idf5.5_py*_env; do
        if [ -d "$d" ]; then IDF_PYTHON_ENV_PATH="$d"; break; fi
    done
fi
if [ -n "${IDF_PYTHON_ENV_PATH:-}" ]; then
    export IDF_PYTHON_ENV_PATH
else
    echo "WARNING: 未找到 ESP-IDF python venv（$HOME/.espressif/python_env/idf5.5_py*_env）。" >&2
    echo "         请先运行：cd $STICKS3_IDF_PATH && IDF_GITHUB_ASSETS=dl.espressif.com/github_assets ./install.sh esp32s3" >&2
fi

# 本机子模块内容由源文件补齐（GitHub 不可达，未走 git submodule init）。
# 使用 ESP-IDF 官方环境变量跳过子模块检查，避免 CMake 触发无法完成的 git clone。
# 依据：tools/cmake/git_submodules.cmake:16。文件已齐，跳过检查是安全的。
export IDF_SKIP_CHECK_SUBMODULES=1

# 用户态 libusb：让 openocd-esp32 能通过 ESP-IDF 的工具校验
if [ -e "$STICKS3_LIBUSB_DIR/libusb-1.0.so.0" ]; then
    export LD_LIBRARY_PATH="$STICKS3_LIBUSB_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
else
    echo "WARNING: 未找到用户态 libusb（$STICKS3_LIBUSB_DIR）。" >&2
    echo "         openocd 校验会失败，export.sh 可能无法激活。参见 README「已知限制」。" >&2
fi

# shellcheck disable=SC1090
. "$STICKS3_IDF_PATH/export.sh"
