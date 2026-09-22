/* asr_credential 单元测试（M3 Selective TDD：RED 先行）
 * 凭据用 TEST 目标名（不触碰真实凭据）；指纹用 NIST 标准向量。
 * 构建：vcvars64 && cl /nologo /W4 /Fe:test_asr_credential.exe asr_credential_test.c asr_credential.c advapi32.lib bcrypt.lib
 */
#include "asr_credential.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        checks += 1;                                                                                                   \
        if (!(cond)) {                                                                                                 \
            failures += 1;                                                                                             \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                                     \
        }                                                                                                              \
    } while (0)

#define TEST(name) printf("== %s ==\n", name)

int main(void) {
    /* 清场：确保 TEST 凭据初始不存在 */
    asr_cred_delete_named(ASR_CRED_TARGET_TEST);

    TEST("SHA-256 指纹：NIST 向量 abc → 尾 8 位 f20015ad");
    {
        char fp[16];
        CHECK(asr_cred_fingerprint("abc", fp, sizeof(fp)) == ASR_CRED_OK);
        CHECK(strlen(fp) == 8);
        CHECK(strcmp(fp, "f20015ad") == 0);
    }

    TEST("指纹：空串（边界）与缓冲不足");
    {
        char fp[16];
        /* SHA-256("") 尾 8 位 = 7852b855 */
        CHECK(asr_cred_fingerprint("", fp, sizeof(fp)) == ASR_CRED_OK);
        CHECK(strcmp(fp, "7852b855") == 0);
        CHECK(asr_cred_fingerprint("abc", fp, 4) == ASR_CRED_PARAM); /* 定长输出，cap<9 属调用方契约违反 */
        CHECK(asr_cred_fingerprint(NULL, fp, sizeof(fp)) == ASR_CRED_PARAM);
    }

    TEST("store → load 往返（UTF-8 Key 原样）");
    {
        static const char key[] = "k1.ABCdef0123456789";
        char out[128];
        CHECK(asr_cred_store_named(ASR_CRED_TARGET_TEST, key) == ASR_CRED_OK);
        CHECK(asr_cred_load_named(ASR_CRED_TARGET_TEST, out, sizeof(out)) == ASR_CRED_OK);
        CHECK(strcmp(out, key) == 0);
        /* 二次存储覆盖语义 */
        CHECK(asr_cred_store_named(ASR_CRED_TARGET_TEST, "second") == ASR_CRED_OK);
        CHECK(asr_cred_load_named(ASR_CRED_TARGET_TEST, out, sizeof(out)) == ASR_CRED_OK);
        CHECK(strcmp(out, "second") == 0);
    }

    TEST("delete 后 load → NOT_FOUND");
    {
        CHECK(asr_cred_delete_named(ASR_CRED_TARGET_TEST) == ASR_CRED_OK);
        char out[128];
        CHECK(asr_cred_load_named(ASR_CRED_TARGET_TEST, out, sizeof(out)) == ASR_CRED_NOT_FOUND);
        CHECK(asr_cred_delete_named(ASR_CRED_TARGET_TEST) == ASR_CRED_NOT_FOUND);
    }

    TEST("参数错误与缓冲不足");
    {
        CHECK(asr_cred_store_named(ASR_CRED_TARGET_TEST, NULL) == ASR_CRED_PARAM);
        CHECK(asr_cred_store_named(ASR_CRED_TARGET_TEST, "") == ASR_CRED_PARAM);
        CHECK(asr_cred_store_named(NULL, "k") == ASR_CRED_PARAM);
        char out[8];
        CHECK(asr_cred_store_named(ASR_CRED_TARGET_TEST, "a-longer-key-value") == ASR_CRED_OK);
        CHECK(asr_cred_load_named(ASR_CRED_TARGET_TEST, out, sizeof(out)) == ASR_CRED_NOMEM);
        asr_cred_delete_named(ASR_CRED_TARGET_TEST);
    }

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
