cmake_minimum_required(VERSION 3.20)

# StickS3 x OpenCode — Windows 主机（M04-M08）
# 目标：C++20 / MSVC 2022 / Win32 + WinRT + WinHTTP（HLD §11）
project(sticks3_host LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT MSVC)
    message(WARNING "本项目主机端锁定 MSVC（HLD §3：已选 Windows C++ 路线）")
endif()

# 工具链自检目标：只证明编译器/链接器/CMake/Ninja 可用，不含产品逻辑。
add_executable(toolchain_check src/toolchain_check.cpp)
target_compile_definitions(toolchain_check PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(toolchain_check PRIVATE user32)

# 产品目标占位（M1 最小 Windows 入口在对应故事中新增，不在此预建空壳）
# add_executable(sticks3_host_tray ...)
