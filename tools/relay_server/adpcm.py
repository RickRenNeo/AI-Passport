"""Pure-Python IMA-ADPCM <-> PCM/WAV codec matching main/ecard_voice.c."""

from __future__ import annotations

import io
import math
import struct
import wave

K_STEP_TABLE = (
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
    253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
)

K_INDEX_TABLE = (-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8)


def _clamp_sample(v: int) -> int:
    if v > 32767:
        return 32767
    if v < -32768:
        return -32768
    return v


def _clamp_index(idx: int) -> int:
    if idx < 0:
        return 0
    if idx > 88:
        return 88
    return idx


def encode_pcm16_to_adpcm(samples: list[int] | tuple[int, ...]) -> bytes:
    """Encode signed 16-bit mono PCM samples into nibble-packed IMA-ADPCM."""
    pred = 0
    idx = 0
    out = bytearray()
    has_low = False
    low = 0

    for raw in samples:
        s = _clamp_sample(int(raw))
        diff = s - pred
        nibble = 0
        if diff < 0:
            nibble = 8
            diff = -diff

        step = K_STEP_TABLE[idx]
        delta = step >> 3
        if diff >= step:
            nibble |= 4
            diff -= step
            delta += step
        if diff >= (step >> 1):
            nibble |= 2
            diff -= step >> 1
            delta += step >> 1
        if diff >= (step >> 2):
            nibble |= 1
            delta += step >> 2

        pred = _clamp_sample(pred + (-delta if (nibble & 8) else delta))
        idx = _clamp_index(idx + K_INDEX_TABLE[nibble])

        if not has_low:
            low = nibble & 0x0F
            has_low = True
        else:
            out.append(low | ((nibble & 0x0F) << 4))
            has_low = False

    if has_low:
        out.append(low)
    return bytes(out)


def decode_adpcm_to_pcm16(adpcm: bytes) -> list[int]:
    """Decode low-nibble-first IMA-ADPCM bytes into signed 16-bit mono samples."""
    pred = 0
    idx = 0
    samples: list[int] = []
    for byte in adpcm:
        for nibble in (byte & 0x0F, (byte >> 4) & 0x0F):
            step = K_STEP_TABLE[idx]
            delta = step >> 3
            if nibble & 4:
                delta += step
            if nibble & 2:
                delta += step >> 1
            if nibble & 1:
                delta += step >> 2
            pred = _clamp_sample(pred + (-delta if (nibble & 8) else delta))
            idx = _clamp_index(idx + K_INDEX_TABLE[nibble])
            samples.append(pred)
    return samples


def adpcm_to_wav_bytes(adpcm: bytes, sample_rate: int = 8000) -> bytes:
    """Wrap decoded IMA-ADPCM into a standard mono 16-bit PCM WAV container."""
    samples = decode_adpcm_to_pcm16(adpcm)
    buf = io.BytesIO()
    with wave.open(buf, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        if samples:
            wf.writeframes(struct.pack(f"<{len(samples)}h", *samples))
    return buf.getvalue()


def wav_bytes_to_adpcm_8k(wav_bytes: bytes) -> tuple[bytes, int]:
    """Convert an arbitrary mono/stereo 16-bit PCM WAV into 8 kHz IMA-ADPCM."""
    buf = io.BytesIO(wav_bytes)
    with wave.open(buf, "rb") as wf:
        channels = wf.getnchannels()
        sampwidth = wf.getsampwidth()
        framerate = wf.getframerate()
        nframes = wf.getnframes()
        raw = wf.readframes(nframes)

    if sampwidth != 2:
        raise ValueError("Only 16-bit PCM WAV is supported")

    count = len(raw) // 2
    vals = struct.unpack(f"<{count}h", raw)
    if channels > 1:
        mono = [
            sum(vals[i : i + channels]) // channels
            for i in range(0, len(vals) - channels + 1, channels)
        ]
    else:
        mono = list(vals)

    if framerate != 8000 and len(mono) > 1:
        target_len = max(1, int(round(len(mono) * 8000.0 / framerate)))
        resampled = []
        for i in range(target_len):
            src_pos = i * (len(mono) - 1) / max(1, target_len - 1)
            lo = int(src_pos)
            hi = min(len(mono) - 1, lo + 1)
            frac = src_pos - lo
            resampled.append(int(round(mono[lo] * (1.0 - frac) + mono[hi] * frac)))
        mono = resampled

    max_samples = 10 * 8000
    mono = mono[:max_samples]
    adpcm = encode_pcm16_to_adpcm(mono)
    duration_sec = max(1, min(10, int(math.ceil(len(mono) / 8000.0))))
    return adpcm, duration_sec


def synthesize_chime_adpcm_8k(duration_sec: int = 3, base_freq: float = 523.25) -> bytes:
    """Generate a pleasant multi-tone 8 kHz IMA-ADPCM chime for tests/simulator."""
    duration_sec = max(1, min(10, int(duration_sec)))
    sample_rate = 8000
    total = duration_sec * sample_rate
    freqs = (base_freq, base_freq * 1.25, base_freq * 1.5, base_freq * 2.0)
    seg = max(1, total // len(freqs))
    samples: list[int] = []
    for i in range(total):
        tone_idx = min(len(freqs) - 1, i // seg)
        local = i % seg
        env = 1.0
        if local < 200:
            env = local / 200.0
        elif local > seg - 400:
            env = max(0.0, (seg - local) / 400.0)
        val = int(9000.0 * env * math.sin(2.0 * math.pi * freqs[tone_idx] * i / sample_rate))
        samples.append(val)
    return encode_pcm16_to_adpcm(samples)
