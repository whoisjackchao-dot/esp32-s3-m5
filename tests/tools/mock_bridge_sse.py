#!/usr/bin/env python3
"""受控上游：以一条构造快照提供 BR-03 SSE（GET /v1/state/stream）。
仅用于刷机后验证 STATE 编码/显示链路（v1.1 用量字段）；非生产组件、不入交付包。
用法: python3 mock_bridge_sse.py [port] [token]   (默认 45100 e2e-token)
"""
import json
import socket
import sys
import threading
import time

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 45100
TOKEN = sys.argv[2] if len(sys.argv) > 2 else "e2e-token"


def snapshot(rev):
    return {
        "status": "busy",
        "sessionID": "ses_e2e_mock",
        "identity": {"snapshotRevision": rev},
        "bind": {"canAppend": True},
        "freshness": {"authoritativeReadOk": True},
        "usage": {
            "input": {"kind": "known", "value": 2445534},
            "output": {"kind": "known", "value": 341798},
            "reasoning": {"kind": "known", "value": 771532},
            "cacheRead": {"kind": "known", "value": 270690560},
            "cacheWrite": {"kind": "known", "value": 12345},
        },
        "cost": {"kind": "known", "value": 1.8468997800000002},
        "project": {"kind": "known", "value": "stick-s3"},
        "sessionTitle": {"kind": "known", "value": "E2E mock"},
        "model": {"current": {"kind": "known", "value": "mock-model"}},
        "tools": [{"count": 2}],
        "todos": {"kind": "known", "value": {"completed": 2, "total": 3}},
    }


def handle(conn):
    try:
        conn.settimeout(5)
        req = b""
        while b"\r\n\r\n" not in req:
            d = conn.recv(4096)
            if not d:
                return
            req += d
        head = req.split(b"\r\n\r\n", 1)[0].decode("latin1")
        first = head.split("\r\n", 1)[0]
        if ("Bearer " + TOKEN) not in head:
            conn.sendall(b"HTTP/1.1 401 Unauthorized\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}")
            return
        if "GET /v1/state/stream" in first:
            conn.sendall(b"HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
                         b"Transfer-Encoding: chunked\r\nConnection: keep-alive\r\n\r\n")
            for i in range(180):
                payload = ("event: snapshot\ndata: " + json.dumps(snapshot(1001 + i)) + "\n\n").encode()
                conn.sendall(("%X\r\n" % len(payload)).encode() + payload + b"\r\n")
                time.sleep(1)
        else:
            body = b'{"alive":true}'
            conn.sendall(b"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                         + str(len(body)).encode() + b"\r\nConnection: close\r\n\r\n" + body)
    except Exception:
        pass
    finally:
        try:
            conn.close()
        except Exception:
            pass


def main():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", PORT))
    srv.listen(8)
    print("mock bridge SSE on 127.0.0.1:%d token=%s" % (PORT, TOKEN), flush=True)
    while True:
        c, _ = srv.accept()
        threading.Thread(target=handle, args=(c,), daemon=True).start()


if __name__ == "__main__":
    main()
