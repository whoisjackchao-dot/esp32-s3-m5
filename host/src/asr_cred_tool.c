/* asr_cred_tool——C11 凭据管理 CLI（托盘交付前的过渡路径）。
 * 用法：
 *   asr_cred_tool.exe store   （隐藏输入 Key，两次确认后存入凭据管理器）
 *   asr_cred_tool.exe show    （仅显示指纹尾 8 位，不显示原文）
 *   asr_cred_tool.exe delete  （删除凭据）
 * 目标名：StickS3/ZhipuASR/ApiKey（C11 冻结值）。
 */
#include "asr_credential.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define KEY_MAX 512

/* 隐藏输入一行（关闭回显，剥除 CR/LF） */
static int read_hidden(char *buf, size_t cap) {
    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0, read = 0;
    if (hin == INVALID_HANDLE_VALUE || !GetConsoleMode(hin, &mode)) return -1;
    if (!SetConsoleMode(hin, (mode & ~ENABLE_ECHO_INPUT) | ENABLE_LINE_INPUT)) return -1;
    if (!ReadConsoleA(hin, buf, (DWORD)(cap - 1), &read, NULL)) {
        SetConsoleMode(hin, mode);
        return -1;
    }
    SetConsoleMode(hin, mode);
    printf("\n");
    while (read > 0 && (buf[read - 1] == '\r' || buf[read - 1] == '\n')) read -= 1;
    buf[read] = '\0';
    return (int)read;
}

int main(int argc, char **argv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc < 2) {
        printf("usage: asr_cred_tool.exe store|show|delete\n");
        return 2;
    }
    if (strcmp(argv[1], "store") == 0) {
        char key[KEY_MAX], again[KEY_MAX];
        printf("请输入智谱API Key（隐藏输入）：");
        if (read_hidden(key, sizeof(key)) <= 0) {
            printf("[FAIL] 未读到 Key\n");
            return 2;
        }
        printf("再输入一次确认：");
        if (read_hidden(again, sizeof(again)) <= 0 || strcmp(key, again) != 0) {
            printf("[FAIL] 两次输入不一致，未存储\n");
            SecureZeroMemory(key, sizeof(key));
            SecureZeroMemory(again, sizeof(again));
            return 2;
        }
        SecureZeroMemory(again, sizeof(again));
        char fp[16];
        if (asr_cred_fingerprint(key, fp, sizeof(fp)) != ASR_CRED_OK) {
            SecureZeroMemory(key, sizeof(key));
            printf("[FAIL] 指纹计算失败\n");
            return 1;
        }
        const asr_cred_status_t st = asr_cred_store(key);
        SecureZeroMemory(key, sizeof(key));
        if (st != ASR_CRED_OK) {
            printf("[FAIL] 存储失败 code=%d\n", st);
            return 1;
        }
        printf("[PASS] 已存入凭据管理器（Key 指纹尾号 …%s）\n", fp);
        return 0;
    }
    if (strcmp(argv[1], "show") == 0) {
        char key[KEY_MAX], fp[16];
        const asr_cred_status_t st = asr_cred_load(key, sizeof(key));
        if (st != ASR_CRED_OK) {
            printf("[FAIL] 读取凭据失败 code=%d（未配置？）\n", st);
            return 1;
        }
        if (asr_cred_fingerprint(key, fp, sizeof(fp)) != ASR_CRED_OK) {
            SecureZeroMemory(key, sizeof(key));
            printf("[FAIL] 指纹计算失败\n");
            return 1;
        }
        printf("[OK] Key 已配置（指纹尾号 …%s），长度 %zu 字节\n", fp, strlen(key));
        SecureZeroMemory(key, sizeof(key));
        return 0;
    }
    if (strcmp(argv[1], "delete") == 0) {
        const asr_cred_status_t st = asr_cred_delete();
        if (st == ASR_CRED_OK) printf("[PASS] 凭据已删除\n");
        else if (st == ASR_CRED_NOT_FOUND) printf("[OK] 凭据本就不存在\n");
        else printf("[FAIL] 删除失败 code=%d\n", st);
        return st == ASR_CRED_OK || st == ASR_CRED_NOT_FOUND ? 0 : 1;
    }
    printf("usage: asr_cred_tool.exe store|show|delete\n");
    return 2;
}
