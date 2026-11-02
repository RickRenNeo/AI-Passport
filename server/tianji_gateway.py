#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
《天机·六爻》AI Passport 语音网关服务
包含 ASR (SenseVoice-Small) + LLM (MiniCPM5-2B-MLX via oMLX) + TTS (EdgeTTS)
"""

import os
import re
import uuid
import asyncio
import logging
import tempfile
import subprocess
from pathlib import Path
from typing import Optional

import requests
import soundfile as sf
import edge_tts
from fastapi import FastAPI, UploadFile, File, Form, HTTPException, Query
from fastapi.responses import FileResponse, JSONResponse
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel

# 配置日志
logging.basicConfig(
    level=logging.INFO,
    format="[%(asctime)s] [%(levelname)s] %(name)s: %(message)s"
)
logger = logging.getLogger("tianji_gateway")

# 环境变量与配置
PORT = int(os.environ.get("PORT", os.environ.get("TIANJI_PORT", 8765)))
OMLX_URL = os.environ.get("OMLX_URL", "http://127.0.0.1:9999/v1/chat/completions")
OMLX_MODEL = os.environ.get("OMLX_MODEL", "MiniCPM5-2B-MLX")
OMLX_API_KEY = os.environ.get("OMLX_API_KEY", "sk-omlx-local")
TTS_VOICE = os.environ.get("TTS_VOICE", "zh-CN-YunxiNeural")

BASE_DIR = Path(__file__).resolve().parent
AUDIO_CACHE_DIR = BASE_DIR / "audio_cache"
AUDIO_CACHE_DIR.mkdir(parents=True, exist_ok=True)

app = FastAPI(
    title="Tianji Liuyao Voice Gateway",
    description="Voice Interaction Hub for FoloToy AI Passport Tianji-Liuyao",
    version="1.0.0"
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# SenseVoice 全局单例
asr_model = None

def get_asr_model():
    global asr_model
    if asr_model is None:
        logger.info("正在加载 SenseVoice-Small 模型...")
        try:
            from funasr import AutoModel
            asr_model = AutoModel(
                model="iic/SenseVoiceSmall",
                trust_remote_code=True,
                device="cpu", # macOS MPS or CPU; CPU is very fast and stable for SenseVoiceSmall
                disable_update=True
            )
            logger.info("SenseVoice-Small 模型加载完成。")
        except Exception as e:
            logger.error(f"加载 SenseVoice 模型失败: {e}", exc_info=True)
            raise
    return asr_model

def clean_sensevoice_text(raw_text: str) -> str:
    """去除 SenseVoice 输出的富文本标记与系统标签"""
    if not raw_text:
        return ""
    # 过滤类似 <|zh|>, <|NEUTRAL|>, <|Speech|>, <|withitn|> 等标签
    cleaned = re.sub(r"<\|.*?\|>", "", raw_text)
    return cleaned.strip()

def check_intent_validity(text: str) -> bool:
    """
    输入有效性判定（关键防线）：
    判定不足：若 ASR 识别出的有效中文字数 < 3 个字，或者仅是“喂、在吗、你好、算一下”等空泛词；
    拦截逻辑：跳过大模型调用，直接将返回状态置为 need_more_info
    """
    if not text:
        return False
    
    # 提取所有中文字符
    chinese_chars = re.findall(r"[\u4e00-\u9fff]", text)
    if len(chinese_chars) < 3:
        return False

    # 去掉所有标点符号与空白
    pure_text = re.sub(r"[^\u4e00-\u9fffA-Za-z0-9]", "", text).lower()
    
    # 常见空泛词/问候词/无实质问题词列表
    vague_tokens = [
        "喂", "你好", "您好", "在吗", "在不在", "有人吗", "哈喽", "hello", "hi", "hey",
        "算一下", "测一下", "算一算", "测一测", "帮我算", "帮我测", "帮我算一下", "帮我测一下",
        "看一下", "看一看", "占卜", "卜卦", "卜一卦", "算卦", "起卦", "解卦", "六爻", "天机",
        "早上好", "中午好", "晚上好", "大师", "先生", "道长", "请问", "问一下", "我想问", "想问"
    ]
    remaining = pure_text
    # 依长度降序替换匹配的空泛词
    for token in sorted(vague_tokens, key=len, reverse=True):
        remaining = remaining.replace(token, "")
    
    # 去除残留的语气助词
    remaining = re.sub(r"[啊呀吗吧呢哦哈嗯额诶啦么了的呀哇\s]+", "", remaining)
    
    # 若剥除空泛问候与助词后，剩余实质内容字符少于 2 个字，则判定为意图不足
    if len(remaining) < 2:
        return False

    return True

NEED_MORE_INFO_PROMPT = (
    "卦无妄动，意在诚敬。所言未明心中所求，请指明欲测之事（如求财、事业、姻缘等），凝神后再禀。"
)

def query_omlx_llm(
    primary_hex: str,
    transformed_hex: str,
    moving_desc: str,
    judgment: str,
    user_question: str
) -> str:
    """调用本地 oMLX MiniCPM5-2B-MLX 解卦"""
    system_prompt = (
        "你是一位深谙周易数理与六爻推演的国学宗师。口吻沉稳洗练、苍劲凝练，洞察天机。"
        "请根据求卦者求测的事项，结合本卦、变卦、动爻及卦辞进行解断。"
        "严格要求：必须直指核心吉凶机锋与化解应对之道，语言古雅而通俗有力，总字数控制在150字以内，切勿啰嗦冗长。"
    )

    user_prompt = (
        f"【求测事项】：{user_question}\n"
        f"【本卦】：{primary_hex}\n"
        f"【变卦】：{transformed_hex}\n"
        f"【动爻说明】：{moving_desc}\n"
        f"【卦辞摘要】：{judgment}\n\n"
        "请宗师推演卦象机变，赐下精炼批断与对策："
    )

    headers = {
        "Authorization": f"Bearer {OMLX_API_KEY}",
        "Content-Type": "application/json"
    }

    payload = {
        "model": OMLX_MODEL,
        "messages": [
            {"role": "system", "content": system_prompt},
            {"role": "user", "content": user_prompt}
        ],
        "temperature": 0.7,
        "max_tokens": 300
    }

    logger.info(f"正在请求 oMLX LLM ({OMLX_URL})...")
    resp = requests.post(OMLX_URL, headers=headers, json=payload, timeout=60)
    resp.raise_for_status()
    data = resp.json()
    reply = data["choices"][0]["message"]["content"].strip()
    return reply

async def generate_edge_tts_wav(text: str) -> tuple[Path, int]:
    """
    通过 edge-tts 合成音频，并转换成 16kHz 16-bit 单声道 WAV 文件。
    返回 (wav_path, duration_ms)
    """
    file_id = f"{uuid.uuid4().hex}.wav"
    output_wav_path = AUDIO_CACHE_DIR / file_id
    temp_mp3_path = AUDIO_CACHE_DIR / f"{uuid.uuid4().hex}.mp3"

    try:
        # 1. 使用 edge-tts 合成 mp3
        communicate = edge_tts.Communicate(text, TTS_VOICE)
        await communicate.save(str(temp_mp3_path))

        # 2. 使用 ffmpeg 转码为标准的 16kHz 16-bit 单声道 WAV
        cmd = [
            "ffmpeg", "-y", "-i", str(temp_mp3_path),
            "-ar", "16000", "-ac", "1", "-c:a", "pcm_s16le",
            str(output_wav_path)
        ]
        proc = await asyncio.create_subprocess_exec(
            *cmd,
            stdout=asyncio.subprocess.DEVNULL,
            stderr=asyncio.subprocess.PIPE
        )
        _, stderr = await proc.communicate()
        if proc.returncode != 0:
            raise RuntimeError(f"ffmpeg 转换音频失败: {stderr.decode('utf-8', errors='ignore')}")

        # 3. 读取 WAV 计算时长 (ms)
        info = sf.info(str(output_wav_path))
        duration_ms = int(info.duration * 1000)
        return output_wav_path, duration_ms

    finally:
        if temp_mp3_path.exists():
            try:
                temp_mp3_path.unlink()
            except Exception:
                pass

def ensure_wav_format(input_bytes: bytes, filename: str = "") -> str:
    """
    确保输入音频格式为符合 SenseVoice 读取的音频文件。
    支持 WAV, PCM, MP3 等，若为 raw PCM 则转为 wav。
    """
    suffix = Path(filename).suffix.lower() if filename else ".wav"
    temp_in = tempfile.NamedTemporaryFile(delete=False, suffix=suffix)
    temp_in.write(input_bytes)
    temp_in.flush()
    temp_in.close()

    temp_out_wav = tempfile.NamedTemporaryFile(delete=False, suffix=".wav")
    temp_out_wav.close()

    try:
        # 如果是 PCM 裸数据（后缀包含 pcm 或无扩展名），假定为 16k 16bit 单声道
        if suffix in [".pcm", ".raw"]:
            cmd = [
                "ffmpeg", "-y", "-f", "s16le", "-ar", "16000", "-ac", "1",
                "-i", temp_in.name,
                "-ar", "16000", "-ac", "1", temp_out_wav.name
            ]
        else:
            # 统一转为 16kHz 单声道 wav
            cmd = [
                "ffmpeg", "-y", "-i", temp_in.name,
                "-ar", "16000", "-ac", "1", temp_out_wav.name
            ]
        res = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        if res.returncode != 0:
            logger.warning(f"ffmpeg 转换输入音频警告，尝试直接使用原文件: {res.stderr.decode('utf-8', errors='ignore')}")
            return temp_in.name
        return temp_out_wav.name
    finally:
        if os.path.exists(temp_in.name) and temp_in.name != temp_out_wav.name:
            try:
                os.unlink(temp_in.name)
            except Exception:
                pass

@app.on_event("startup")
async def startup_event():
    logger.info("正在启动《天机·六爻》语音网关...")
    # 异步或后台初始化模型，或直接拉取
    try:
        get_asr_model()
    except Exception as e:
        logger.warning(f"启动时预加载 ASR 模型跳过或出错 (将在首次请求时重试): {e}")

@app.get("/health")
def health_check():
    return {"status": "ok", "service": "tianji_gateway"}

@app.get("/api/v1/audio/download")
def download_audio(id: str = Query(..., description="音频文件标识，如 xxxx.wav")):
    # 安全校验：防止路径遍历
    clean_id = os.path.basename(id)
    file_path = AUDIO_CACHE_DIR / clean_id
    if not file_path.exists() or not file_path.is_file():
        raise HTTPException(status_code=404, detail="音频文件不存在或已失效")
    return FileResponse(path=str(file_path), media_type="audio/wav", filename=clean_id)

@app.post("/api/v1/divination/voice")
async def divination_voice(
    audio_file: UploadFile = File(..., description="用户语音音频文件"),
    primary_hex: str = Form(..., description="本卦名称"),
    transformed_hex: str = Form(..., description="变卦名称"),
    moving_desc: str = Form(..., description="动爻说明"),
    judgment: str = Form(..., description="卦辞摘要")
):
    """
    语音解卦核心流：
    1. ASR 语音识别
    2. 有效性拦截
    3. LLM 解卦（如有效）
    4. EdgeTTS 合成返回
    """
    logger.info(f"收到解卦请求: 本卦={primary_hex}, 变卦={transformed_hex}, 动爻={moving_desc}")
    
    # 1. 保存上传音频并规范化为 WAV
    audio_bytes = await audio_file.read()
    if not audio_bytes:
        raise HTTPException(status_code=400, detail="上传音频文件为空")

    wav_input_path = ensure_wav_format(audio_bytes, audio_file.filename or "input.wav")

    # 2. 语音识别 (ASR)
    user_question = ""
    try:
        model = get_asr_model()
        # FunASR SenseVoiceSmall 推理
        res = model.generate(
            input=wav_input_path,
            cache={},
            language="auto",
            use_itn=True,
            batch_size_s=60,
            merge_vad=True,
            merge_length_s=15
        )
        if res and len(res) > 0:
            raw_text = res[0].get("text", "")
            user_question = clean_sensevoice_text(raw_text)
        logger.info(f"ASR 识别结果: '{user_question}'")
    except Exception as e:
        logger.error(f"ASR 识别过程异常: {e}", exc_info=True)
        user_question = ""
    finally:
        if os.path.exists(wav_input_path):
            try:
                os.unlink(wav_input_path)
            except Exception:
                pass

    # 3. 输入有效性判定
    is_valid = check_intent_validity(user_question)

    if not is_valid:
        logger.info(f"用户意图不足或空泛 ('{user_question}')，触发引导提示。")
        text_reply = NEED_MORE_INFO_PROMPT
        status = "need_more_info"
    else:
        # 4. LLM 解卦
        status = "success"
        try:
            llm_reply = query_omlx_llm(
                primary_hex=primary_hex,
                transformed_hex=transformed_hex,
                moving_desc=moving_desc,
                judgment=judgment,
                user_question=user_question
            )
            text_reply = llm_reply
        except Exception as e:
            logger.error(f"调用 oMLX 解卦失败: {e}", exc_info=True)
            text_reply = "天机晦明，数理交感有阻。暂依卦象之德修身以待，少歇后再试。"

    # 5. EdgeTTS 语音合成
    try:
        wav_file_path, duration_ms = await generate_edge_tts_wav(text_reply)
        audio_filename = wav_file_path.name
        audio_url = f"/api/v1/audio/download?id={audio_filename}"
    except Exception as e:
        logger.error(f"TTS 合成失败: {e}", exc_info=True)
        raise HTTPException(status_code=500, detail=f"TTS 合成失败: {str(e)}")

    response_data = {
        "status": status,
        "user_question": user_question,
        "text_reply": text_reply,
        "audio_url": audio_url,
        "duration_ms": duration_ms
    }
    logger.info(f"解卦完成响应: status={status}, duration_ms={duration_ms}")
    return JSONResponse(content=response_data)


if __name__ == "__main__":
    import uvicorn
    uvicorn.run("server.tianji_gateway:app", host="0.0.0.0", port=PORT, reload=False)
