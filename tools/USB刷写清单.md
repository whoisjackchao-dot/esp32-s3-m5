# StickS3 USB 刷写清单（M2）

> 适用：M5Stack StickS3（ESP32-S3-PICO-1-N8R8，8MB Flash）。固件源：`firmware/sticks3`。
> 首次烧录与升级均走 USB（R21，无 OTA）。控制台与烧录同口（USB-Serial-JTAG）。

## 0. 前置

| 项 | 值 |
| --- | --- |
| 数据口 | USB Type-C（设备正面），PC 侧枚举为 COMx（Windows） |
| Windows Python | 3.12（`C:\Users\shiwenc\AppData\Local\Programs\Python\Python312\python.exe`，含 esptool） |
| WSL 构建环境 | `tools/idf-env.sh`（ESP-IDF v5.5.5 + xtensa-gcc 14.2.0） |
| 端口号 | 本文以 COM5 为例，以实际枚举为准 |

## 1. 构建（WSL）

```bash
cd /mnt/c/Temp/ESP32-S3M5/firmware/sticks3
source /mnt/c/Temp/ESP32-S3M5/tools/idf-env.sh
idf.py -B /tmp/esp-sticks3-build build
cp /tmp/esp-sticks3-build/bootloader/bootloader.bin \
   /tmp/esp-sticks3-build/partition_table/partition-table.bin \
   /tmp/esp-sticks3-build/sticks3.bin /mnt/c/Temp/ESP32-S3M5/flash-artifacts/
sha256sum /mnt/c/Temp/ESP32-S3M5/flash-artifacts/sticks3.bin
```

## 2. 首次烧录前的强制备份（REL-02）

```powershell
$PY = 'C:\Users\shiwenc\AppData\Local\Programs\Python\Python312\python.exe'
& $PY -m esptool --chip esp32s3 -p COM5 --baud 921600 read-flash 0 0x800000 C:\Temp\ StickS3-backup\factory-flash-8MB.bin
```

记录 `factory-flash-8MB.bin` 的 SHA256 并存档于仓库外。**此备份是恢复出厂的唯一途径。**

## 3. 烧录（三件套 @ 偏移）

```powershell
& $PY -m esptool --chip esp32s3 -p COM5 --baud 921600 --before default-reset --after hard-reset `
  write-flash `
  0x0     C:\Temp\ESP32-S3M5\flash-artifacts\bootloader.bin `
  0x8000  C:\Temp\ESP32-S3M5\flash-artifacts\partition-table.bin `
  0x10000 C:\Temp\ESP32-S3M5\flash-artifacts\sticks3.bin
```

成功判据：`Hash of data verified.` + `Hard resetting via RTS pin...`

## 4. 分区表（出厂布局）

| 名称 | 类型 | 偏移 | 大小 |
| --- | --- | --- | --- |
| nvs | data/nvs | 0x9000 | 24K |
| phy_init | data/phy | 0xf000 | 4K |
| factory | app | 0x10000 | 1M |

注意：固件升级**不擦除** NVS（bond 存储在其中）。若 NVS 被外部污染（如旧固件残留导致
`ESP_ERR_NVS_PAGE_FULL`），单独擦除：`esptool erase-region 0x9000 0x6000`（会清空全部 bond，
需重新配对）。全量恢复出厂：`esptool write-flash --flash-mode dio --flash-freq 80m --flash-size 8MB`
写回第 2 步备份镜像。

## 5. 启动验证（115200 波特，同一 COM 口）

健康日志关键字：

```
sticks3: chip model : 9 (9 = ESP32-S3)
sticks3: s3p_proto selftest: PASS (SV-01/02/03/05)
audio_capture: es8311 ready (16kHz/16bit, addr 0x18)
ble_gatt: advertising: svc 22587f80-334a-53d6-beb8-8a6ace31af07
display: 屏幕就绪：ST7789 135x240 gap(52,40) invert 背光开
```

屏幕：白闪 → 三原色横条自检 → 状态帧（STICKS3 / STATE 等）。

## 6. 当前固件行为快照（与本清单同步更新）

- READY(0x10) 触发录音会话：500 帧 / 30 秒硬停（TX-02），Opus 24kbps 60ms
- 会话流 BEGIN→AUDIO×N→END（summary=FNV-1a 全部 Opus 载荷字节）
- STATE(0x12) 位图为 M2 纵切草案（bit0=busy bit1=valid），M4 冻结
- MTU<65 拒绝录音（ERROR 事件）；音频 init 失败时会话门控拒绝（不发 BEGIN）
