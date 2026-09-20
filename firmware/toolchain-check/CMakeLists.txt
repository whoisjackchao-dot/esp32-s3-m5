cmake_minimum_required(VERSION 3.16)

# StickS3 x OpenCode — 固件（M01-M03）
# 工具链自检工程：只证明 ESP-IDF 5.5.x 能为 esp32s3 构建。
# 产品固件在 M2 对应故事中新增；此处不含音频/BLE/屏幕逻辑。

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sticks3_toolchain_check)
