#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""设备串口日志采集（跨断电重启，自动重连）。
用法：python device_log.py [COM口] [秒数] [输出文件]
默认：COM5 150s C:\\Temp\\sticks3_boot.log
说明：USB-Serial-JTAG 在正常启动时会重枚举，主机需主动重连；本工具循环重开端口。
"""
import datetime
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM5"
SECONDS = int(sys.argv[2]) if len(sys.argv) > 2 else 150
OUT = sys.argv[3] if len(sys.argv) > 3 else r"C:\Temp\sticks3_boot.log"

with open(OUT, "w", encoding="utf-8", errors="replace") as f:
    f.write("START %s\n" % datetime.datetime.now().strftime("%H:%M:%S"))
    f.flush()
    end = time.time() + SECONDS
    s = None
    while time.time() < end:
        if s is None:
            try:
                s = serial.Serial(PORT, 115200, timeout=0.5)
                s.dtr = True
                s.rts = True
                f.write("OPEN %s\n" % datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3])
                f.flush()
            except Exception:
                time.sleep(0.3)
                continue
        try:
            b = s.read(1024)
            if b:
                ts = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
                f.write("[%s] %s\n" % (ts, b.decode("utf-8", "replace").rstrip()))
                f.flush()
        except Exception:
            try:
                s.close()
            except Exception:
                pass
            s = None
    f.write("END %s\n" % datetime.datetime.now().strftime("%H:%M:%S"))
print("LOG_WRITTEN:", OUT)
