#!/usr/bin/env python3
"""Unit and HTTP integration tests for tools/relay_server."""

from __future__ import annotations

import base64
import io
import json
import math
import socket
import struct
import sys
import threading
import unittest
import urllib.error
import urllib.request
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "relay_server"))

from adpcm import adpcm_to_wav_bytes, decode_adpcm_to_pcm16, encode_pcm16_to_adpcm, wav_bytes_to_adpcm_8k
from server import RelayState, UdpDiscoveryResponder, create_server


class RelayServerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.now = [1760000000]
        self.state = RelayState(clock=lambda: self.now[0])
        self.httpd, _ = create_server("127.0.0.1", 0, state=self.state)
        self.port = self.httpd.server_address[1]
        self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)
        self.thread.start()
        self.base = f"http://127.0.0.1:{self.port}"

    def tearDown(self) -> None:
        self.httpd.shutdown()
        self.httpd.server_close()

    def _req(self, method: str, path: str, body: dict | bytes | None = None,
             headers: dict[str, str] | None = None) -> tuple[int, bytes]:
        data = None
        hdrs = dict(headers or {})
        if isinstance(body, dict):
            data = json.dumps(body).encode("utf-8")
            hdrs.setdefault("Content-Type", "application/json")
        elif isinstance(body, bytes):
            data = body
        req = urllib.request.Request(f"{self.base}{path}", data=data, method=method, headers=hdrs)
        try:
            with urllib.request.urlopen(req, timeout=5) as resp:
                return resp.status, resp.read()
        except urllib.error.HTTPError as err:
            payload = err.read()
            err.close()
            return err.code, payload

    def test_adpcm_and_wav_roundtrip(self) -> None:
        sine = [int(8000.0 * math.sin(2.0 * math.pi * 440.0 * i / 8000.0)) for i in range(4000)]
        adp = encode_pcm16_to_adpcm(sine)
        self.assertEqual(len(adp), 2000)
        dec = decode_adpcm_to_pcm16(adp)
        self.assertEqual(len(dec), 4000)
        self.assertGreater(max(abs(x) for x in dec), 4000)

        # Wrap as 16 kHz WAV -> downsample/encode to 8 kHz ADPCM
        buf = io.BytesIO()
        with wave.open(buf, "wb") as wf:
            wf.setnchannels(1)
            wf.setsampwidth(2)
            wf.setframerate(16000)
            s16 = [int(6000.0 * math.sin(2.0 * math.pi * 300.0 * i / 16000.0)) for i in range(16000)]
            wf.writeframes(struct.pack(f"<{len(s16)}h", *s16))
        adp8, dur = wav_bytes_to_adpcm_8k(buf.getvalue())
        self.assertEqual(dur, 1)
        self.assertEqual(len(adp8), 4000)
        wav_out = adpcm_to_wav_bytes(adp8, sample_rate=8000)
        self.assertTrue(wav_out.startswith(b"RIFF"))

    def test_acl_and_zero_retention_lifecycle(self) -> None:
        dev_id = "aa:bb:cc:dd:ee:ff"
        code, raw = self._req("POST", "/api/v1/device/poll", {
            "device_id": dev_id,
            "role": "student",
            "student_id": "20260001",
            "student_name": "张小明",
            "school": "第一实验小学",
            "class_name": "三年级二班",
            "primary_phone": "13800138000",
        })
        self.assertEqual(code, 200)
        poll1 = json.loads(raw)
        self.assertEqual(poll1["messages"], [])

        # Update ACL: bind family + class teacher + school admin
        code, _ = self._req("POST", "/api/v1/client/acl", {
            "device_id": dev_id,
            "acl": {
                "primary_phone": "13800138000",
                "student_school": "第一实验小学",
                "student_class": "三年级二班",
                "family": [{"phone": "13800138001", "label": "爸爸"}],
                "teachers": [{"id": "T1", "name": "李老师", "phone": "13900001111", "school": "第一实验小学", "class_name": "三年级二班"}],
                "admins": [{"id": "A1", "name": "陈主任", "phone": "13900002222", "school": "第一实验小学"}],
            },
        })
        self.assertEqual(code, 200)

        # Unauthorized family phone / other class teacher / other school admin rejected (403)
        for bad in (
            {"sender_role": "family", "sender_name": "陌生", "sender_phone": "19900000000"},
            {"sender_role": "teacher", "sender_name": "外班", "sender_phone": "13900001111", "sender_school": "第一实验小学", "sender_class": "三年级一班"},
            {"sender_role": "school_admin", "sender_name": "外校", "sender_phone": "13900002222", "sender_school": "第二小学"},
        ):
            bad["device_id"] = dev_id
            c, _ = self._req("POST", "/api/v1/client/send", bad)
            self.assertEqual(c, 403)

        # Authorized senders succeed
        rids = []
        for good in (
            {"sender_role": "primary_parent", "sender_name": "妈妈", "sender_phone": "13800138000", "text": "妈妈留言"},
            {"sender_role": "family", "sender_name": "爸爸", "sender_phone": "13800138001", "text": "爸爸留言"},
            {"sender_role": "teacher", "sender_name": "李老师", "sender_phone": "13900001111", "sender_school": "第一实验小学", "sender_class": "三年级二班", "text": "老师通知"},
            {"sender_role": "school_admin", "sender_name": "陈主任", "sender_phone": "13900002222", "sender_school": "第一实验小学", "text": "全校通知"},
        ):
            good["device_id"] = dev_id
            c, r = self._req("POST", "/api/v1/client/send", good)
            self.assertEqual(c, 200)
            rids.append(json.loads(r)["remote_id"])

        # Device polls, downloads audio, and ACKs -> server immediately purges
        c, raw = self._req("POST", "/api/v1/device/poll", {"device_id": dev_id})
        self.assertEqual(c, 200)
        msgs = json.loads(raw)["messages"]
        self.assertEqual(len(msgs), 4)

        c, audio_bytes = self._req("GET", f"/api/v1/device/audio/{rids[0]}?device_id={dev_id}")
        self.assertEqual(c, 200)
        self.assertGreater(len(audio_bytes), 1000)

        c, ack_raw = self._req("POST", "/api/v1/device/ack", {"device_id": dev_id, "remote_ids": rids})
        self.assertEqual(c, 200)
        self.assertEqual(json.loads(ack_raw)["deleted"], 4)
        self.assertEqual(json.loads(ack_raw)["remaining_to_device"], 0)

        # Audio no longer exists on server after ACK
        c, _ = self._req("GET", f"/api/v1/device/audio/{rids[0]}?device_id={dev_id}")
        self.assertEqual(c, 404)

    def test_upload_asr_client_ack_and_ttl(self) -> None:
        adp = encode_pcm16_to_adpcm([1000, -1000] * 2000)
        c, raw = self._req(
            "POST",
            "/api/v1/device/upload?device_id=dev1&student_id=1&student_name=Test&duration_sec=2&audio_format=adpcm_8k",
            body=adp,
            headers={"Content-Type": "application/octet-stream"},
        )
        self.assertEqual(c, 200)
        up = json.loads(raw)
        self.assertTrue(up["ok"])
        self.assertTrue(len(up["text"]) > 0)
        rid = up["remote_id"]

        c, wav_raw = self._req("GET", f"/api/v1/client/audio/{rid}")
        self.assertEqual(c, 200)
        self.assertTrue(wav_raw.startswith(b"RIFF"))

        # Client ACK immediately deletes uploaded message & audio
        c, ack_raw = self._req("POST", "/api/v1/client/ack", {"remote_ids": [rid]})
        self.assertEqual(c, 200)
        self.assertEqual(json.loads(ack_raw)["remaining_to_client"], 0)
        c, _ = self._req("GET", f"/api/v1/client/audio/{rid}")
        self.assertEqual(c, 404)

        # 24h TTL auto-expiration for undelivered messages
        self._req("POST", "/api/v1/device/poll", {"device_id": "dev2", "primary_phone": "13800138000"})
        c, _ = self._req("POST", "/api/v1/client/send", {
            "device_id": "dev2",
            "sender_role": "primary_parent",
            "sender_phone": "13800138000",
            "text": "TTL测试",
        })
        self.assertEqual(c, 200)
        self.now[0] += 86401
        c, st_raw = self._req("GET", "/api/v1/client/state")
        self.assertEqual(c, 200)
        self.assertEqual(json.loads(st_raw)["ephemeral_queues"]["to_device_count"], 0)

    def test_udp_discovery_responder(self) -> None:
        udp = UdpDiscoveryResponder(http_port=self.port, discover_port=0, override_url="http://192.168.1.10:8787")
        udp.start_socket()
        assert udp._sock is not None
        dport = udp._sock.getsockname()[1]
        udp.start()
        try:
            cli = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            cli.settimeout(2.0)
            cli.sendto(b"AI_PASSPORT_RELAY_DISCOVER", ("127.0.0.1", dport))
            data, _ = cli.recvfrom(512)
            cli.close()
            obj = json.loads(data.decode("utf-8"))
            self.assertEqual(obj["service"], "ai-passport-relay")
            self.assertEqual(obj["url"], "http://192.168.1.10:8787")
        finally:
            udp.stop()


if __name__ == "__main__":
    unittest.main()
