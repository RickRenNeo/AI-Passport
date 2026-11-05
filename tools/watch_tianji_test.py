#!/usr/bin/env python3
"""Watch the AI-Passport serial log and summarise one on-device divination test.

Runs until it has seen the voice flow (or a long timeout), then prints a compact
timeline so nobody has to babysit the device:

    submit latency, inter-sentence gaps, no-voice cancels, follow-up decisions.
"""
import os
import re
import sys
import time

LOG = "build/serial_tianji.log"
TIMEOUT_S = 3600
QUIET_EXIT_S = 90          # after the device goes quiet for this long, we call the test over
MIN_EVENTS = 3

PATTERNS = [
    ("phase", r"tianji_bridge: 语音阶段 (\d) -> (\d) \((.*)\)"),
    ("stt", r"Application: >> (.*)"),
    ("tts", r"Application: << (.*)"),
    ("submit", r"tianji_bridge: (用户按键|静音超时)，提交本轮录音"),
    ("first", r"tianji_bridge: 首句断语送达 (\d+) ms"),
    ("gap", r"tianji_bridge: 断语句间隔 (\d+) ms"),
    ("silence", r"tianji_bridge: 连续静音 (\d+) ms"),
    ("novoice", r"tianji_bridge: 等候 (\d+) ms 未开口"),
    ("empty", r"tianji_bridge: 本轮未检测到人声"),
    ("followup", r"tianji_bridge: 断语播毕，自动开启下一轮聆听"),
    ("ttsdone", r"tianji_bridge: 宗师诵读结束"),
    ("quiet_end", r"tianji_bridge: 宗师 (\d+) ms 内未回应"),
    ("fail", r"tianji_bridge: 本轮语音失败: (.*)"),
    ("tool_noise", r"tianji_bridge: 忽略工具调用残留句子"),
    ("tool_ctx", r"tianji_bridge: 服务端 LLM 调取卦象上下文 \((\d+) 字节\)"),
    ("heartbeat", r"SystemInfo: free sram"),
]


def main():
    deadline = time.time() + TIMEOUT_S
    events = []
    last_event = time.time()
    pos = 0
    while time.time() < deadline:
        if not os.path.exists(LOG):
            time.sleep(1)
            continue
        with open(LOG, "rb") as f:
            f.seek(pos)
            chunk = f.read()
            pos = f.tell()
        if chunk:
            text = chunk.decode("utf-8", "replace")
            for line in text.splitlines():
                for kind, pat in PATTERNS:
                    m = re.search(pat, line)
                    if not m:
                        continue
                    if kind == "heartbeat":
                        continue
                    events.append((kind, m.groups(), line.strip()))
                    last_event = time.time()
                    print(f"[{time.strftime('%H:%M:%S')}] {line.strip()}", flush=True)
                    break
        now = time.time()
        if len(events) >= MIN_EVENTS and now - last_event > QUIET_EXIT_S:
            break
        time.sleep(0.5)

    print("\n===== SUMMARY =====", flush=True)
    if not events:
        print("no voice activity in this window", flush=True)
        return
    submits = [e for e in events if e[0] == "submit"]
    firsts = [e for e in events if e[0] == "first"]
    gaps = [e for e in events if e[0] == "gap"]
    novoice = [e for e in events if e[0] in ("novoice", "empty")]
    followups = [e for e in events if e[0] == "followup"]
    quiet = [e for e in events if e[0] == "quiet_end"]
    fails = [e for e in events if e[0] == "fail"]
    noise = [e for e in events if e[0] == "tool_noise"]
    ctx = [e for e in events if e[0] == "tool_ctx"]
    print(f"rounds submitted      : {len(submits)}", flush=True)
    print(f"first-sentence latency: {[int(e[1][0]) for e in firsts]}", flush=True)
    print(f"sentence gaps (ms)    : {[int(e[1][0]) for e in gaps]}", flush=True)
    print(f"no-voice cancels      : {len(novoice)}", flush=True)
    print(f"auto follow-ups       : {len(followups)}", flush=True)
    print(f"quiet timeouts        : {len(quiet)}", flush=True)
    print(f"hard failures         : {[e[1][0] for e in fails]}", flush=True)
    print(f"tool noise filtered   : {len(noise)}", flush=True)
    print(f"tool context calls    : {[(int(e[1][0])) for e in ctx]}", flush=True)


if __name__ == "__main__":
    sys.exit(main())
