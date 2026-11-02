#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
《天机·六爻》语音网关自动化测试脚本
测试场景：
1. 意图不足/空泛词测试：验证触发 need_more_info 与引导语
2. 完整意图测试：验证 SenseVoice ASR + oMLX MiniCPM5 + EdgeTTS 完整链路
3. 音频下载接口测试：验证 GET /api/v1/audio/download
"""

import os
import sys
import time
import asyncio
import subprocess
import requests
from pathlib import Path

GATEWAY_URL = os.environ.get("GATEWAY_URL", "http://127.0.0.1:8765")
TESTS_DIR = Path(__file__).resolve().parent

async def create_test_audio(text: str, output_wav_path: Path):
    """使用 edge-tts 生成测试语音并转成 16k 16-bit 单声道 WAV"""
    import edge_tts
    temp_mp3 = output_wav_path.with_suffix(".mp3")
    try:
        comm = edge_tts.Communicate(text, "zh-CN-YunxiNeural")
        await comm.save(str(temp_mp3))
        cmd = [
            "ffmpeg", "-y", "-i", str(temp_mp3),
            "-ar", "16000", "-ac", "1", "-c:a", "pcm_s16le",
            str(output_wav_path)
        ]
        res = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        if res.returncode != 0:
            raise RuntimeError(f"FFmpeg 生成测试音频失败: {res.stderr.decode('utf-8')}")
    finally:
        if temp_mp3.exists():
            temp_mp3.unlink()

def test_gateway():
    print("=" * 60)
    print("开始执行《天机·六爻》语音网关集成测试...")
    print(f"目标网关地址: {GATEWAY_URL}")
    print("=" * 60)

    # 1. 检查网关连通性
    for retry in range(10):
        try:
            r = requests.get(f"{GATEWAY_URL}/health", timeout=5)
            if r.status_code == 200:
                print("网关服务就绪 (/health OK)")
                break
        except Exception:
            time.sleep(2)
    else:
        print("错误: 无法连接到网关服务，请确认服务已启动！")
        sys.exit(1)

    # 2. 生成测试音频
    vague_wav = TESTS_DIR / "sample_vague.wav"
    valid_wav = TESTS_DIR / "sample_valid.wav"

    print("\n[准备测试音频]")
    asyncio.run(create_test_audio("喂，你好啊", vague_wav))
    print(f"  - 生成意图不足音频: {vague_wav}")
    asyncio.run(create_test_audio("我想测测下半年事业变动", valid_wav))
    print(f"  - 生成完整意图音频: {valid_wav}")

    # 3. 测试场景一：空泛意图拦截
    print("\n" + "-" * 50)
    print("【测试场景 1】意图不足/空泛词拦截测试 (输入: '喂，你好啊')")
    t0 = time.time()
    with open(vague_wav, "rb") as f:
        resp = requests.post(
            f"{GATEWAY_URL}/api/v1/divination/voice",
            files={"audio_file": ("vague.wav", f, "audio/wav")},
            data={
                "primary_hex": "乾为天",
                "transformed_hex": "天风姤",
                "moving_desc": "初九动",
                "judgment": "乾：元，亨，利，贞。"
            },
            timeout=30
        )
    elapsed_1 = time.time() - t0
    assert resp.status_code == 200, f"请求失败: {resp.text}"
    data_1 = resp.json()
    print(f"响应耗时: {elapsed_1:.2f}s")
    print(f"返回数据: {data_1}")
    assert data_1["status"] == "need_more_info", f"预期 status 为 need_more_info，实际为 {data_1['status']}"
    assert "指明欲测之事" in data_1["text_reply"], "未能返回预期引导语"
    assert data_1.get("audio_url"), "未生成引导语 TTS 音频"
    print(">>> 场景 1 验证通过！成功触发意图不足拦截与引导。")

    # 4. 测试场景二：完整意图解卦 (ASR -> LLM -> TTS)
    print("\n" + "-" * 50)
    print("【测试场景 2】完整意图全链路解卦测试 (输入: '我想测测下半年事业变动')")
    t0 = time.time()
    with open(valid_wav, "rb") as f:
        resp = requests.post(
            f"{GATEWAY_URL}/api/v1/divination/voice",
            files={"audio_file": ("valid.wav", f, "audio/wav")},
            data={
                "primary_hex": "乾为天",
                "transformed_hex": "天风姤",
                "moving_desc": "初九动",
                "judgment": "乾：元，亨，利，贞。"
            },
            timeout=60
        )
    elapsed_2 = time.time() - t0
    assert resp.status_code == 200, f"请求失败: {resp.text}"
    data_2 = resp.json()
    print(f"响应耗时: {elapsed_2:.2f}s")
    print(f"ASR 转写: '{data_2.get('user_question')}'")
    print(f"解卦状态: {data_2.get('status')}")
    print(f"宗师批断:\n{data_2.get('text_reply')}")
    print(f"音频链接: {data_2.get('audio_url')}")
    print(f"音频时长: {data_2.get('duration_ms')} ms")
    assert data_2["status"] == "success", f"预期 status 为 success，实际为 {data_2['status']}"
    assert len(data_2["text_reply"]) > 10, "解卦内容过短"
    assert data_2.get("audio_url"), "未返回 audio_url"
    print(">>> 场景 2 验证通过！成功识别语音、调用 MiniCPM5 完成解卦并生成 TTS。")

    # 5. 测试场景三：音频下载校验
    print("\n" + "-" * 50)
    print("【测试场景 3】测试音频下载接口 GET /api/v1/audio/download")
    audio_url = data_2["audio_url"]
    download_resp = requests.get(f"{GATEWAY_URL}{audio_url}", timeout=10)
    assert download_resp.status_code == 200, f"音频下载失败: {download_resp.status_code}"
    assert download_resp.headers.get("content-type") == "audio/wav", "音频 content-type 必须为 audio/wav"
    assert len(download_resp.content) > 1000, "下载的音频文件过小"
    print(f"音频下载成功，文件大小: {len(download_resp.content)} 字节")
    print(">>> 场景 3 验证通过！")

    # 清理测试生成的本地样本
    for p in [vague_wav, valid_wav]:
        if p.exists():
            p.unlink()

    print("\n" + "=" * 60)
    print("所有集成测试项全部通过！服务运行正常。")
    print("=" * 60)

if __name__ == "__main__":
    test_gateway()
