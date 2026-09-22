// TEST-API-01 受控 ASR HTTP 服务（mock）。
// 规格：tests/README.md「受控 ASR 服务规格」；行为测试：mock-asr.test.ts。
// 边界：仅本机回环监听；不得进入生产网络面；不作为识别质量证据。
const MAGIC = 0x4f503353; // 保留：与 S3P 工具链同前缀风格，便于日志辨识
const VALID_SCENARIOS = new Set([
  "ok",
  "empty_text",
  "invalid_json",
  "http_401",
  "http_429",
  "http_500",
  "http_503",
]);
const ASR_PATH = "/api/paas/v4/audio/transcriptions";
const DEFAULT_TEXT = "测试语音转写文本";

interface MockConfig {
  scenario: string;
  text: string;
  delayMs: number;
}

interface RecordedRequest {
  path: string;
  method: string;
  contentType: string;
  fields: {
    model?: string;
    stream?: string;
    file?: { filename: string; type: string; size: number };
    unexpected: Record<string, string>;
  };
  headers: Record<string, string>;
}

const KNOWN_FIELDS = new Set(["model", "stream", "file"]);

export interface MockAsrServer {
  port: number;
  stop(): Promise<void>;
}

export async function createMockAsrServer(
  opts: { port?: number; host?: string } = {},
): Promise<MockAsrServer> {
  let config: MockConfig = { scenario: "ok", text: DEFAULT_TEXT, delayMs: 0 };
  let last: RecordedRequest | null = null;

  const json = (status: number, body: unknown) =>
    new Response(JSON.stringify(body), {
      status,
      headers: { "content-type": "application/json" },
    });

  const server = Bun.serve({
    hostname: opts.host ?? "127.0.0.1",
    port: opts.port ?? 0,
    idleTimeout: 30,
    async fetch(req) {
      const url = new URL(req.url);

      if (url.pathname === "/__mock/config" && req.method === "POST") {
        const body = (await req.json()) as Record<string, unknown>;
        const scenario = String(body.scenario ?? "ok");
        if (!VALID_SCENARIOS.has(scenario)) {
          return json(400, { error: "unknown scenario", scenario });
        }
        config = {
          scenario,
          text: typeof body.text === "string" ? body.text : DEFAULT_TEXT,
          delayMs: typeof body.delayMs === "number" ? body.delayMs : 0,
        };
        return new Response(null, { status: 204 });
      }

      if (url.pathname === "/__mock/reset" && req.method === "POST") {
        last = null;
        config = { scenario: "ok", text: DEFAULT_TEXT, delayMs: 0 };
        return new Response(null, { status: 204 });
      }

      if (url.pathname === "/__mock/last" && req.method === "GET") {
        return json(
          200,
          last ?? { path: "", method: "", contentType: "", fields: { unexpected: {} }, headers: {} },
        );
      }

      if (url.pathname === ASR_PATH && req.method === "POST") {
        // 记录实际请求字段（数据最小化审计：多余字段必须可见）
        const contentType = req.headers.get("content-type") ?? "";
        const fields: RecordedRequest["fields"] = { unexpected: {} };
        try {
          const fd = await req.formData();
          for (const [name, value] of fd.entries()) {
            if (name === "file" && value instanceof File) {
              fields.file = {
                filename: value.name,
                type: value.type,
                size: value.size,
              };
            } else if (KNOWN_FIELDS.has(name)) {
              fields[name] = String(value);
            } else {
              fields.unexpected[name] = String(value);
            }
          }
        } catch {
          fields.unexpected["__parse_error"] = "multipart parse failed";
        }
        last = {
          path: url.pathname,
          method: req.method,
          contentType,
          fields,
          headers: Object.fromEntries(req.headers.entries()),
        };

        if (config.delayMs > 0) {
          await Bun.sleep(config.delayMs);
        }

        switch (config.scenario) {
          case "empty_text":
            return json(200, {
              id: "mock", created: Date.now(), model: "glm-asr-2512", text: "",
            });
          case "invalid_json":
            return new Response("not-json{{{", {
              status: 200,
              headers: { "content-type": "application/json" },
            });
          case "http_401":
            return json(401, { error: { code: "1001", message: "mock auth" } });
          case "http_429":
            return json(429, { error: { code: "1113", message: "mock arrears" } });
          case "http_500":
            return json(500, { error: { code: "1302", message: "mock busy" } });
          case "http_503":
            return json(503, { error: { code: "1305", message: "mock busy" } });
          default: {
            const model = fields.model ?? "glm-asr-2512";
            return json(200, {
              id: "mock", created: Date.now(), model, text: config.text,
            });
          }
        }
      }

      return json(404, { error: "not found", path: url.pathname });
    },
  });

  void MAGIC;
  return { port: server.port, stop: () => server.stop(true) };
}
