// mock-asr 独立入口：bun serve.ts <port> <host>
// 例：bun serve.ts 18765 0.0.0.0（供 Windows 侧测试跨 WSL 访问）
import { createMockAsrServer } from "./server.ts";

const port = Number(process.argv[2] ?? 18765);
const host = process.argv[3] ?? "127.0.0.1";

const srv = await createMockAsrServer({ port, host });
console.log(`mock-asr listening on http://${host}:${srv.port}`);
