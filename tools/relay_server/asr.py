"""Pluggable ASR provider for AI Passport relay server.

Supports:
1. Tencent Cloud One-Sentence Recognition (`SentenceRecognition` via TC3-HMAC-SHA256)
   when `TENCENTCLOUD_SECRET_ID` and `TENCENTCLOUD_SECRET_KEY` are present in env.
2. External HTTP webhook ASR when `AI_PASSPORT_ASR_URL` is configured.
3. Lightweight offline fallback description when no cloud ASR credentials are set,
   so LAN closed-loop verification always returns deterministic UTF-8 text.
"""

from __future__ import annotations

import base64
import hashlib
import hmac
import json
import os
import time
import urllib.request
from datetime import datetime, timezone

from adpcm import adpcm_to_wav_bytes, decode_adpcm_to_pcm16


def _tc3_sign(secret_key: str, date: str, service: str, string_to_sign: str) -> str:
    def _hmac(key: bytes, msg: str) -> bytes:
        return hmac.new(key, msg.encode("utf-8"), hashlib.sha256).digest()

    secret_date = _hmac(("TC3" + secret_key).encode("utf-8"), date)
    secret_service = _hmac(secret_date, service)
    secret_signing = _hmac(secret_service, "tc3_request")
    return hmac.new(secret_signing, string_to_sign.encode("utf-8"), hashlib.sha256).hexdigest()


def tencent_sentence_recognition(wav_bytes: bytes, sample_rate: int,
                                 secret_id: str, secret_key: str,
                                 region: str = "ap-shanghai") -> str:
    host = "asr.tencentcloudapi.com"
    service = "asr"
    action = "SentenceRecognition"
    version = "2019-06-14"
    timestamp = int(time.time())
    date = datetime.fromtimestamp(timestamp, tz=timezone.utc).strftime("%Y-%m-%d")

    engine = "16k_zh" if sample_rate >= 16000 else "8k_zh"
    payload = json.dumps({
        "EngSerViceType": engine,
        "SourceType": 1,
        "VoiceFormat": "wav",
        "Data": base64.b64encode(wav_bytes).decode("ascii"),
        "DataLen": len(wav_bytes),
    }, separators=(",", ":"))

    canonical_headers = f"content-type:application/json; charset=utf-8\nhost:{host}\n"
    signed_headers = "content-type;host"
    hashed_payload = hashlib.sha256(payload.encode("utf-8")).hexdigest()
    canonical_request = (
        f"POST\n/\n\n{canonical_headers}\n{signed_headers}\n{hashed_payload}"
    )
    credential_scope = f"{date}/{service}/tc3_request"
    string_to_sign = (
        f"TC3-HMAC-SHA256\n{timestamp}\n{credential_scope}\n"
        + hashlib.sha256(canonical_request.encode("utf-8")).hexdigest()
    )
    signature = _tc3_sign(secret_key, date, service, string_to_sign)
    authorization = (
        f"TC3-HMAC-SHA256 Credential={secret_id}/{credential_scope}, "
        f"SignedHeaders={signed_headers}, Signature={signature}"
    )

    req = urllib.request.Request(
        f"https://{host}/",
        data=payload.encode("utf-8"),
        method="POST",
        headers={
            "Authorization": authorization,
            "Content-Type": "application/json; charset=utf-8",
            "Host": host,
            "X-TC-Action": action,
            "X-TC-Timestamp": str(timestamp),
            "X-TC-Version": version,
            "X-TC-Region": region,
        },
    )
    with urllib.request.urlopen(req, timeout=8) as resp:
        data = json.loads(resp.read().decode("utf-8"))
    result = data.get("Response", {}).get("Result", "")
    return str(result).strip()


def transcribe_adpcm(adpcm: bytes, audio_format: str = "adpcm_8k",
                     duration_sec: int = 0) -> tuple[str, str]:
    """Return `(transcript_text, engine_name)` for an uploaded ADPCM voice clip."""
    sample_rate = 16000 if audio_format == "adpcm_16k" else 8000
    wav_bytes = adpcm_to_wav_bytes(adpcm, sample_rate=sample_rate)

    secret_id = os.environ.get("TENCENTCLOUD_SECRET_ID", "").strip()
    secret_key = os.environ.get("TENCENTCLOUD_SECRET_KEY", "").strip()
    if secret_id and secret_key:
        try:
            text = tencent_sentence_recognition(wav_bytes, sample_rate, secret_id, secret_key)
            if text:
                return text, "tencent_asr"
        except Exception as exc:
            # Degrade gracefully rather than failing message delivery
            fallback_note = f"语音留言({duration_sec}秒，云端识别暂不可用)"
            return f"{fallback_note}", f"tencent_asr_error:{type(exc).__name__}"

    asr_url = os.environ.get("AI_PASSPORT_ASR_URL", "").strip()
    if asr_url:
        try:
            req = urllib.request.Request(
                asr_url,
                data=wav_bytes,
                method="POST",
                headers={"Content-Type": "audio/wav"},
            )
            with urllib.request.urlopen(req, timeout=8) as resp:
                obj = json.loads(resp.read().decode("utf-8"))
            text = str(obj.get("text", "")).strip()
            if text:
                return text, "custom_asr"
        except Exception:
            pass

    samples = decode_adpcm_to_pcm16(adpcm)
    peak = max((abs(s) for s in samples), default=0)
    sec = duration_sec if duration_sec > 0 else max(1, round(len(samples) / sample_rate))
    if peak < 180:
        return f"学生语音留言 {sec} 秒（环境较安静）", "local_fallback"
    return f"学生已发送 {sec} 秒语音留言（请点击播放收听原声）", "local_fallback"
