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

# M1 最小 Windows 入口：向本地桥接发送一次 draft.append（契约 BR-04）
add_executable(sticks3_host_entry src/host_entry.cpp)
target_compile_definitions(sticks3_host_entry PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(sticks3_host_entry PRIVATE winhttp)

# M4 托盘（S4.7）：Win32 托盘壳 + 桥接健康 + Key 菜单（无 WinRT 依赖）
add_executable(tray src/tray.cpp)
set_target_properties(tray PROPERTIES WIN32_EXECUTABLE TRUE)
target_compile_definitions(tray PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(tray PRIVATE user32 shell32 winhttp)
