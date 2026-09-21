cmake_minimum_required(VERSION 3.16)

# StickS3 固件（M01 采音 / M02 BLE 安全传输 / M03 屏幕与电源）
# M2 骨架：s3p_proto 线格式（C07 冻结 v1.0）可构建；
# 采音/Opus/NimBLE 在对应故事中按 Selective TDD 增量进入。
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sticks3)
