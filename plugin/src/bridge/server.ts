import type {
    AppendOutcome,
    AppendRequest,
    BridgeServer,
    BridgeServerOptions,
    QueryOutcome,
} from "../core/types";
import { BODY_MAX_BYTES, PROTOCOL_VERSION } from "../core/types";

/** 响应体全部由本模块构造，故仅以 unknown 承载并统一序列化 */
function jsonResponse(status: number, body: unknown): Response {
    return new Response(JSON.stringify(body), {
        status,
        headers: { "content-type": "application/json" },
    });
}

function errorBody(code: string): { error: { code: string } } {
    return { error: { code } };
}

/** 契约 §3：仅做 schema 校验；TEXT_MAX_LEN 等语义校验归协调器 */
function isAppendRequest(value: unknown): value is AppendRequest {
    if (typeof value !== "object" || value === null) return false;
    const r = value as Record<string, unknown>;
    return (
        r.protocolVersion === PROTOCOL_VERSION &&
        typeof r.tuiEpoch === "string" &&
        typeof r.sessionID === "string" &&
        typeof r.operationID === "string" &&
        typeof r.transactionGeneration === "number" &&
        typeof r.text === "string"
    );
}

function appendHttpStatus(outcome: AppendOutcome): number {
    switch (outcome.status) {
        case "applied":
        case "uncertain":
            return 200;
        case "processing":
            return 202;
        case "rejected":
        case "conflict":
        case "stale_epoch":
            return 409;
        case "capacity_exceeded":
            return 429;
    }
}

function appendResponseBody(req: AppendRequest, outcome: AppendOutcome): unknown {
    return {
        protocolVersion: PROTOCOL_VERSION,
        tuiEpoch: req.tuiEpoch,
        operationID: req.operationID,
        status: outcome.status,
        reason: outcome.status === "rejected" ? outcome.reason : null,
    };
}

export function startBridge(options: BridgeServerOptions): BridgeServer {
    const streams = new Set<ReadableStreamDefaultController<Uint8Array>>();
    let stopped = false;

    async function handle(req: Request, boundPort: number): Promise<Response> {
        // §2 检查顺序：认证(401) → Origin(403) → Host(403)，均先于路由与载荷校验
        if (req.headers.get("authorization") !== `Bearer ${options.token}`) {
            return jsonResponse(401, errorBody("unauthorized"));
        }
        if (req.headers.get("origin") !== null) {
            return jsonResponse(403, errorBody("origin_forbidden"));
        }
        const host = req.headers.get("host");
        if (host !== `127.0.0.1:${boundPort}` && host !== `localhost:${boundPort}`) {
            return jsonResponse(403, errorBody("host_forbidden"));
        }

        const path = new URL(req.url).pathname;
        const method = req.method;

        if (path === "/v1/health") {
            if (method !== "GET") return jsonResponse(405, errorBody("method_not_allowed"));
            return jsonResponse(200, {
                protocolVersion: PROTOCOL_VERSION,
                pluginVersion: options.pluginVersion,
                tuiEpoch: options.tuiEpoch,
                alive: true,
            });
        }

        if (path === "/v1/binding") {
            if (method !== "GET") return jsonResponse(405, errorBody("method_not_allowed"));
            return jsonResponse(200, options.handlers.binding());
        }

        if (path === "/v1/draft/append") {
            if (method !== "POST") return jsonResponse(405, errorBody("method_not_allowed"));
            const mediaType = (req.headers.get("content-type") ?? "").split(";")[0]?.trim().toLowerCase() ?? "";
            if (mediaType !== "application/json") {
                return jsonResponse(415, errorBody("unsupported_media_type"));
            }
            const raw = await req.arrayBuffer();
            if (raw.byteLength > BODY_MAX_BYTES) {
                return jsonResponse(400, errorBody("payload_too_large"));
            }
            let parsed: unknown;
            try {
                parsed = JSON.parse(new TextDecoder().decode(raw));
            } catch (error) {
                void error;
                return jsonResponse(400, errorBody("invalid_request"));
            }
            if (!isAppendRequest(parsed)) {
                return jsonResponse(400, errorBody("invalid_request"));
            }
            const outcome = await options.handlers.append(parsed);
            return jsonResponse(appendHttpStatus(outcome), appendResponseBody(parsed, outcome));
        }

        if (path.startsWith("/v1/operations/")) {
            const segments = path.split("/");
            const operationID = segments[3];
            if (operationID !== undefined && operationID.length > 0 && segments.length === 4) {
                if (method !== "GET") return jsonResponse(405, errorBody("method_not_allowed"));
                const outcome: QueryOutcome = options.handlers.query(options.tuiEpoch, operationID);
                if (outcome.status === "unknown") {
                    return jsonResponse(404, errorBody("unknown_operation"));
                }
                const body: unknown =
                    outcome.reason === undefined
                        ? { status: outcome.status }
                        : { status: outcome.status, reason: outcome.reason };
                return jsonResponse(200, body);
            }
            if (
                operationID !== undefined &&
                operationID.length > 0 &&
                segments.length === 5 &&
                segments[4] === "cancel"
            ) {
                if (method !== "POST") return jsonResponse(405, errorBody("method_not_allowed"));
                const outcome = options.handlers.cancel(options.tuiEpoch, operationID);
                if (outcome.status === "identity_error") {
                    return jsonResponse(409, { status: "identity_error" });
                }
                return jsonResponse(200, { status: outcome.status });
            }
            return jsonResponse(404, errorBody("not_found"));
        }

        if (path === "/v1/state/stream") {
            if (method !== "GET") return jsonResponse(405, errorBody("method_not_allowed"));
            const encoder = new TextEncoder();
            let controllerRef: ReadableStreamDefaultController<Uint8Array> | undefined;
            const stream = new ReadableStream<Uint8Array>({
                start(controller) {
                    controllerRef = controller;
                    streams.add(controller);
                    // M1 最小实现：连接注释 + 一次完整快照；心跳/增量归 M4
                    controller.enqueue(encoder.encode(": connected\n\n"));
                    controller.enqueue(
                        encoder.encode(`event: snapshot\ndata: ${JSON.stringify(options.handlers.binding())}\n\n`),
                    );
                },
                cancel() {
                    if (controllerRef) streams.delete(controllerRef);
                },
            });
            return new Response(stream, {
                status: 200,
                headers: { "content-type": "text/event-stream" },
            });
        }

        return jsonResponse(404, errorBody("not_found"));
    }

    let boundPort = options.port ?? 0;
    const server: Bun.Server<never> = Bun.serve({
        hostname: "127.0.0.1",
        port: boundPort,
        fetch: (req): Promise<Response> => handle(req, boundPort),
    });
    boundPort = server.port ?? boundPort;

    return {
        port: boundPort,
        hostname: "127.0.0.1",
        stop() {
            if (stopped) return;
            stopped = true;
            const closeErrors: unknown[] = [];
            for (const controller of streams) {
                try {
                    controller.close();
                } catch (error) {
                    closeErrors.push(error);
                }
            }
            streams.clear();
            server.stop();
            if (closeErrors.length > 0) {
                // 已断开流重复关闭属预期竞态；仅计数，不外泄任何内容
                console.error(`sticks3-bridge: ${closeErrors.length} stream(s) already closed`);
            }
        },
    };
}
