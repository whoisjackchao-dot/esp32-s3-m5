// TEST-API-01 受控 ASR 服务——mock 自身的行为测试（先红后绿）。
// 规格：tests/README.md「受控 ASR 服务规格」。
// 边界：仅本机回环，不得访问生产网络；不作为识别质量证据。
import { afterEach, describe, expect, test } from "bun:test";

import { createMockAsrServer, type MockAsrServer } from "./server.ts";

let srv: MockAsrServer;

async function start() {
  srv = await createMockAsrServer({ port: 0 });
}

afterEach(async () => {
  if (srv) await srv.stop();
});

async function config(body: Record<string, unknown>) {
  const res = await fetch(`http://127.0.0.1:${srv.port}/__mock/config`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(body),
  });
  expect(res.status).toBe(204);
}

function asrUrl() {
  return `http://127.0.0.1:${srv.port}/api/paas/v4/audio/transcriptions`;
}

function multipartRequest(): RequestInit {
  const fd = new FormData();
  fd.set("model", "glm-asr-2512");
  fd.set("stream", "false");
  fd.set("file", new Blob([new Uint8Array(64).fill(0x7)], { type: "audio/wav" }), "t.wav");
  return { method: "POST", body: fd };
}

async function lastRequest() {
  const res = await fetch(`http://127.0.0.1:${srv.port}/__mock/last`);
  return (await res.json()) as {
    path: string;
    method: string;
    fields: {
      model?: string;
      stream?: string;
      file?: { filename: string; type: string; size: number };
      unexpected: Record<string, string>;
    };
  };
}

describe("TEST-API-01 mock-asr", () => {
  test("默认场景：200 + 非空 text + 回显请求中的 model", async () => {
    await start();
    const res = await fetch(asrUrl(), multipartRequest());
    expect(res.status).toBe(200);
    const json = await res.json();
    expect(typeof json.text).toBe("string");
    expect(json.text.length).toBeGreaterThan(0);
    expect(json.model).toBe("glm-asr-2512");
  });

  test("场景 empty_text：200 + text 为空串", async () => {
    await start();
    await config({ scenario: "empty_text" });
    const res = await fetch(asrUrl(), multipartRequest());
    expect(res.status).toBe(200);
    const json = await res.json();
    expect(json.text).toBe("");
  });

  test("场景 invalid_json：200 + 畸形 body", async () => {
    await start();
    await config({ scenario: "invalid_json" });
    const res = await fetch(asrUrl(), multipartRequest());
    expect(res.status).toBe(200);
    const raw = await res.text();
    expect(() => JSON.parse(raw)).toThrow();
  });

  test.each([401, 429, 500])("场景 http_%i：返回该状态码", async (code) => {
    await start();
    await config({ scenario: `http_${code}` });
    const res = await fetch(asrUrl(), multipartRequest());
    expect(res.status).toBe(code);
  });

  test("场景 delay：响应延迟不低于注入值", async () => {
    await start();
    await config({ scenario: "ok", delayMs: 300 });
    const t0 = Date.now();
    const res = await fetch(asrUrl(), multipartRequest());
    expect(res.status).toBe(200);
    expect(Date.now() - t0).toBeGreaterThanOrEqual(280);
  });

  test("记录实际请求：路径、multipart 字段与多余字段", async () => {
    await start();
    const fd = multipartRequest().body as FormData;
    fd.set("unexpected_field", "should-not-exist");
    await fetch(asrUrl(), { method: "POST", body: fd });
    const last = await lastRequest();
    expect(last.method).toBe("POST");
    expect(last.path).toBe("/api/paas/v4/audio/transcriptions");
    expect(last.fields.model).toBe("glm-asr-2512");
    expect(last.fields.stream).toBe("false");
    expect(last.fields.file?.filename).toBe("t.wav");
    expect(last.fields.file?.type).toContain("wav");
    expect(last.fields.file?.size).toBe(64);
    expect(last.fields.unexpected["unexpected_field"]).toBe("should-not-exist");
  });

  test("reset 清空已记录请求", async () => {
    await start();
    await fetch(asrUrl(), multipartRequest());
    await fetch(`http://127.0.0.1:${srv.port}/__mock/reset`, { method: "POST" });
    const last = await lastRequest();
    expect(last.method).toBe("");
  });

  test("未知场景：config 拒绝（400）", async () => {
    await start();
    const res = await fetch(`http://127.0.0.1:${srv.port}/__mock/config`, {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ scenario: "nope" }),
    });
    expect(res.status).toBe(400);
  });
});
