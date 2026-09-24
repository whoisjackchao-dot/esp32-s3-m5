#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Q04 语料采集编排器（纯 stdlib；不做 ASR——参考文本先于任何 ASR 输出冻结）。

驱动 15 句人工采集：逐句打印横幅 → 操作员按 Enter → 外壳调用既有
button_hil.py（BLE 采集，退出码 0=合法/2=失败）→ decode_capture.py（Opus→48kHz
mono WAV，退出码 0=成功）→ 计算 sha256/时长 → 增量写完成稿到 --out。

本脚本只编排 subprocess + 文件 + 提示，绝不重实现 BLE/Opus。
用法（Windows Python 3.12）：
  python tools/q04_capture.py --list
  python tools/q04_capture.py [--only 7] [--ambient 安静室内 --distance 0.5]
"""
import argparse
import hashlib
import json
import subprocess
import sys
import wave
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
BUTTON_HIL = HERE / "button_hil.py"
DECODE_CAPTURE = HERE / "decode_capture.py"
WAV_FRAMERATE = 48000  # decode_capture.py (libopus) 固定输出 48kHz


def is_placeholder(value):
    if value is None:
        return True
    if isinstance(value, str):
        s = value.strip()
        return not s or s.startswith("<待填")
    return False


def select_samples(samples, only):
    if only is None:
        return list(samples)
    if not 1 <= only <= len(samples):
        raise ValueError("--only={} 超出 1..{}".format(only, len(samples)))
    return [samples[only - 1]]


def sha256_of_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def duration_of_wav(path):
    with wave.open(str(path), "rb") as w:
        return w.getnframes() / w.getframerate()


def apply_capture(sample, wav_path, s3pcap_path, sha, duration, ambient, distance):
    """纯函数：返回填好采集结果的新 dict，不改动传入的 sample。"""
    updated = dict(sample)
    updated["wav"] = str(wav_path)
    updated["wav_sha256"] = sha
    updated["duration_s"] = duration
    updated["ambient_noise"] = ambient
    updated["device_distance_m"] = distance
    updated["audio_environment"] = "ambient={}; distance={}m".format(ambient, distance)
    updated["speaker_authorized"] = True
    return updated


def run_capture(sample, outdir, begin_timeout, end_timeout, run=subprocess.run):
    s3pcap = Path(outdir) / "{}.s3pcap".format(sample["id"])
    argv = [sys.executable, str(BUTTON_HIL), str(begin_timeout), str(end_timeout), str(s3pcap)]
    proc = run(argv)
    return proc.returncode == 0, s3pcap


def decode(s3pcap, wav, run=subprocess.run):
    argv = [sys.executable, str(DECODE_CAPTURE), str(s3pcap), str(wav)]
    return run(argv).returncode == 0


def load_draft(path):
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


def load_completed_or_initial(args):
    return load_draft(args.out) if args.out.exists() else load_draft(args.draft)


def save_draft(path, doc):
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(doc, fh, ensure_ascii=False, indent=2)
        fh.write("\n")


def parse_distance(text):
    return float(text)


def prompt_missing(ambient, distance, ask=input):
    if ambient is None:
        ambient = ask("环境噪声（ambient_noise，如 安静室内）: ").strip()
    if distance is None:
        while True:
            try:
                distance = parse_distance(ask("设备距离（device_distance_m，米，如 0.5）: "))
                break
            except ValueError:
                print("[FAIL] 请输入数字，例如 0.5")
    return ambient, distance


def build_parser():
    ap = argparse.ArgumentParser(description="Q04 语料采集编排器（capture only，无 ASR）")
    ap.add_argument("--draft", type=Path, default=HERE / "Q04语料草稿-20260924.json",
                    help="语料草稿（只读，绝不就地修改）")
    ap.add_argument("--out", type=Path, default=HERE / "Q04语料草稿-采集完成.json",
                    help="采集完成稿输出路径（每句成功后增量保存）")
    ap.add_argument("--outdir", type=Path, default=REPO / "captures" / "q04",
                    help="wav/s3pcap 输出目录")
    ap.add_argument("--begin-timeout", type=int, default=120)
    ap.add_argument("--end-timeout", type=int, default=45)
    ap.add_argument("--only", type=int, default=None, help="只采集第 N 句（1 基）")
    ap.add_argument("--list", action="store_true", help="打印 15 句后退出")
    ap.add_argument("--ambient", default=None, help="ambient_noise（省略则交互询问一次）")
    ap.add_argument("--distance", type=float, default=None,
                    help="device_distance_m（省略则交互询问一次）")
    return ap


def main(argv=None):
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    args = build_parser().parse_args(argv)
    doc = load_completed_or_initial(args)
    samples = doc["samples"]

    if args.list:
        for i, s in enumerate(samples, 1):
            print("{}. [{}] {}".format(i, s["_组"], s["reference_text"]))
        return 0

    try:
        selected = select_samples(samples, args.only)
    except ValueError as exc:
        print("[FAIL] {}".format(exc))
        return 2
    selected_ids = {s["id"] for s in selected}
    args.outdir.mkdir(parents=True, exist_ok=True)

    try:
        ambient, distance = prompt_missing(args.ambient, args.distance)
    except (EOFError, KeyboardInterrupt):
        print("\n[FAIL] 操作员中止")
        return 2

    ok_ids, failed_ids = [], []
    for i, sample in enumerate(samples, 1):
        if sample["id"] not in selected_ids:
            continue
        s3pcap = args.outdir / "{}.s3pcap".format(sample["id"])
        wav = args.outdir / "{}.wav".format(sample["id"])
        print("=" * 62)
        print("[{}/{}] id={} 组={}".format(i, len(samples), sample["id"], sample["_组"]))
        print("句子: {}".format(sample["reference_text"]))
        print("解码输出: {}Hz mono 16bit WAV（不重采样）".format(WAV_FRAMERATE))
        try:
            input("按 Enter 开始；看到 button_hil 就绪后【按住按键→说出上面句子→松开】> ")
        except (EOFError, KeyboardInterrupt):
            print("\n[FAIL] 操作员中止")
            return 2

        ok, _ = run_capture(sample, args.outdir, args.begin_timeout, args.end_timeout)
        if not ok:
            failed_ids.append(sample["id"])
            print("[FAIL] {} button_hil 退出码非 0，标记失败并继续".format(sample["id"]))
            continue
        if not decode(s3pcap, wav):
            failed_ids.append(sample["id"])
            print("[FAIL] {} decode_capture 退出码非 0，标记失败并继续".format(sample["id"]))
            continue

        sha = sha256_of_file(wav)
        dur = duration_of_wav(wav)
        samples[i - 1] = apply_capture(sample, wav, s3pcap, sha, dur, ambient, distance)
        save_draft(args.out, doc)
        ok_ids.append(sample["id"])
        print("[OK] {} sha={}… duration={:.2f}s → 已增量保存 {}".format(
            sample["id"], sha[:12], dur, args.out))

    print("=" * 62)
    print("[SUMMARY] 成功 {}/{}: {}".format(len(ok_ids), len(selected_ids),
                                            " ".join(ok_ids) or "-"))
    print("[SUMMARY] 失败 {}: {}".format(len(failed_ids), " ".join(failed_ids) or "-"))
    return 0 if not failed_ids else 1


if __name__ == "__main__":
    sys.exit(main())
