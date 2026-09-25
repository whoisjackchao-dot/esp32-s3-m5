// M4 S4.2 —— BR-03 → STATE 字段映射实现。
#include "state_merge.h"

#include <string.h>

static void copy_text(char *dst, const char *src) {
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t n = strlen(src);
    if (n > S3P_STATE_MAX_TEXT) {
        n = S3P_STATE_MAX_TEXT;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

void state_merge(s3p_state_snapshot *out, const br03_snapshot *in) {
    memset(out, 0, sizeof(*out));
    out->status = in->status;

    uint8_t flags = 0;
    if (in->can_append) {
        flags |= S3P_FLAG_CAN_APPEND;
    }
    if (in->expired) {
        flags |= S3P_FLAG_EXPIRED;
    }
    if (in->tool_failed) {
        flags |= S3P_FLAG_TOOL_FAILED;
    }
    if (in->child_busy) {
        flags |= S3P_FLAG_CHILD_BUSY;
    }
    if (in->project && in->project[0] != '\0') {
        flags |= S3P_FLAG_HAS_PROJECT;
        copy_text(out->project, in->project);
    }
    if (in->title && in->title[0] != '\0') {
        flags |= S3P_FLAG_HAS_TITLE;
        copy_text(out->title, in->title);
    }
    if (in->model && in->model[0] != '\0') {
        flags |= S3P_FLAG_HAS_MODEL;
        copy_text(out->model, in->model);
    }
    const uint8_t usage_known = (in->tokens_input != S3P_STATE_UNKNOWN) ||
                                (in->tokens_output != S3P_STATE_UNKNOWN) ||
                                (in->cost_milli != S3P_STATE_UNKNOWN);
    if (usage_known) {
        flags |= S3P_FLAG_HAS_USAGE;
    }
    out->flags = flags;

    out->revision = in->revision;
    out->tool_count = in->tool_count;
    if (in->todo_known) {
        out->todo_completed = in->todo_completed;
        out->todo_total = in->todo_total;
    }
    out->tokens_input = in->tokens_input;
    out->tokens_output = in->tokens_output;
    out->cost_milli = in->cost_milli;
}
