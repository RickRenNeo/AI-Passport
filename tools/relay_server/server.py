#!/usr/bin/env python3
"""Zero-retention ephemeral relay server + WeChat Mini Program simulator for AI Passport."""

from __future__ import annotations

import argparse
import base64
import json
import os
import socket
import sys
import threading
import time
import uuid
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

from adpcm import (
    adpcm_to_wav_bytes,
    synthesize_chime_adpcm_8k,
    wav_bytes_to_adpcm_8k,
)
from asr import transcribe_adpcm

TTL_SECONDS = 86400
DEFAULT_HTTP_PORT = 8787
DEFAULT_DISCOVER_PORT = 8788


def detect_lan_ip(peer_ip: str | None = None) -> str:
    """Return the local interface IP reachable from `peer_ip` (or default LAN route)."""
    targets = [peer_ip] if peer_ip else []
    targets.extend(["192.168.1.1", "172.16.0.1", "10.0.0.1", "8.8.8.8"])
    for target in targets:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.connect((target, 9))
            ip = sock.getsockname()[0]
            sock.close()
            if ip and not ip.startswith("127."):
                return ip
        except OSError:
            continue
    return "127.0.0.1"


class RelayState:
    """Thread-safe ephemeral in-memory relay state (zero retention after ACK)."""

    def __init__(self, clock=time.time) -> None:
        self._lock = threading.RLock()
        self._clock = clock
        self.devices: dict[str, dict] = {}
        self.acls: dict[str, dict] = {}
        # Ephemeral outbound-to-device messages: device_id -> {remote_id: msg_dict}
        self.to_device: dict[str, dict[str, dict]] = {}
        # Ephemeral inbound-to-client messages (from device): remote_id -> msg_dict
        self.to_client: dict[str, dict] = {}
        self.sos_alerts: list[dict] = []
        self.audit_log: list[dict] = []

    def now(self) -> int:
        return int(self._clock())

    def _log(self, event: str, detail: str) -> None:
        self.audit_log.insert(0, {
            "time": self.now(),
            "event": event,
            "detail": detail,
        })
        del self.audit_log[60:]

    def purge_expired_locked(self) -> int:
        cutoff = self.now() - TTL_SECONDS
        removed = 0
        for dev_id, bucket in list(self.to_device.items()):
            for rid in [k for k, m in bucket.items() if m["created_epoch"] < cutoff]:
                del bucket[rid]
                removed += 1
        for rid in [k for k, m in self.to_client.items() if m["created_epoch"] < cutoff]:
            del self.to_client[rid]
            removed += 1
        if removed:
            self._log("TTL_EXPIRE", f"Auto-expired {removed} undelivered message(s) >24h")
        return removed

    def _ensure_acl_locked(self, device_id: str) -> dict:
        if device_id not in self.acls:
            self.acls[device_id] = {
                "primary_phone": "",
                "student_school": "",
                "student_class": "",
                "family": [],
                "teachers": [],
                "admins": [],
            }
        return self.acls[device_id]

    def resolve_device_id(self, requested: str | None) -> str | None:
        with self._lock:
            if requested and requested not in ("auto", "default", "*"):
                return requested
            if self.devices:
                # Pick most recently seen device
                return max(self.devices.items(), key=lambda kv: kv[1].get("last_seen", 0))[0]
            if self.acls:
                return next(iter(self.acls))
            return requested if requested not in ("auto", "default", "*", None, "") else None

    def device_poll(self, payload: dict) -> dict:
        with self._lock:
            self.purge_expired_locked()
            dev_id = str(payload.get("device_id") or "unknown").strip()
            now_ep = self.now()
            self.devices[dev_id] = {
                "device_id": dev_id,
                "role": str(payload.get("role") or "student"),
                "student_id": str(payload.get("student_id") or ""),
                "student_name": str(payload.get("student_name") or ""),
                "school": str(payload.get("school") or ""),
                "class_name": str(payload.get("class_name") or ""),
                "primary_phone": str(payload.get("primary_phone") or ""),
                "last_seen": now_ep,
            }
            acl = self._ensure_acl_locked(dev_id)
            if payload.get("primary_phone") and not acl["primary_phone"]:
                acl["primary_phone"] = str(payload["primary_phone"])
            if payload.get("school") and not acl["student_school"]:
                acl["student_school"] = str(payload["school"])
            if payload.get("class_name") and not acl["student_class"]:
                acl["student_class"] = str(payload["class_name"])

            bucket = self.to_device.get(dev_id, {})
            messages = []
            for m in sorted(bucket.values(), key=lambda item: item["created_epoch"]):
                messages.append({
                    "remote_id": m["remote_id"],
                    "sender_role": m["sender_role"],
                    "sender_name": m["sender_name"],
                    "sender_phone": m["sender_phone"],
                    "sender_school": m["sender_school"],
                    "sender_class": m["sender_class"],
                    "text": m["text"],
                    "duration_sec": m["duration_sec"],
                    "audio_format": m["audio_format"],
                    "created_epoch": m["created_epoch"],
                })
            return {
                "ok": True,
                "server_time": now_ep,
                "acl": acl,
                "messages": messages,
            }

    def get_device_audio(self, remote_id: str, device_id: str | None = None) -> bytes | None:
        with self._lock:
            self.purge_expired_locked()
            if device_id and device_id in self.to_device:
                m = self.to_device[device_id].get(remote_id)
                return m["audio"] if m else None
            for bucket in self.to_device.values():
                if remote_id in bucket:
                    return bucket[remote_id]["audio"]
            return None

    def device_ack(self, device_id: str, remote_ids: list[str]) -> dict:
        with self._lock:
            bucket = self.to_device.get(device_id, {})
            deleted = 0
            for rid in remote_ids:
                if rid in bucket:
                    del bucket[rid]
                    deleted += 1
                else:
                    # Also scan across buckets if device_id omitted/changed
                    for b in self.to_device.values():
                        if rid in b:
                            del b[rid]
                            deleted += 1
                            break
            if deleted:
                self._log(
                    "DEVICE_ACK_DELETE",
                    f"Device {device_id} ACKed {deleted} msg(s); purged from server RAM (0 bytes retained)",
                )
            remaining = sum(len(b) for b in self.to_device.values())
            return {
                "ok": True,
                "deleted": deleted,
                "remaining_to_device": remaining,
                "server_time": self.now(),
            }

    def device_upload(self, params: dict, adpcm_bytes: bytes) -> dict:
        dev_id = str(params.get("device_id") or "unknown")
        student_id = str(params.get("student_id") or "")
        student_name = str(params.get("student_name") or "学生")
        school = str(params.get("school") or "")
        class_name = str(params.get("class_name") or "")
        primary_phone = str(params.get("primary_phone") or "")
        audio_format = str(params.get("audio_format") or "adpcm_8k")
        try:
            duration_sec = max(1, min(15, int(params.get("duration_sec") or 1)))
        except ValueError:
            duration_sec = 1

        text, engine = transcribe_adpcm(adpcm_bytes, audio_format=audio_format, duration_sec=duration_sec)
        remote_id = f"up_{uuid.uuid4().hex[:10]}"

        with self._lock:
            self.purge_expired_locked()
            now_ep = self.now()
            self.to_client[remote_id] = {
                "remote_id": remote_id,
                "device_id": dev_id,
                "student_id": student_id,
                "student_name": student_name,
                "school": school,
                "class_name": class_name,
                "primary_phone": primary_phone,
                "duration_sec": duration_sec,
                "audio_format": audio_format,
                "audio": adpcm_bytes,
                "text": text,
                "asr_engine": engine,
                "created_epoch": now_ep,
            }
            self._log(
                "DEVICE_UPLOAD",
                f"{student_name} uploaded {len(adpcm_bytes)}B ({duration_sec}s), ASR[{engine}]={text}",
            )
            return {
                "ok": True,
                "remote_id": remote_id,
                "text": text,
                "asr_engine": engine,
                "server_time": now_ep,
            }

    def device_sos(self, payload: dict) -> dict:
        with self._lock:
            now_ep = self.now()
            alert = {
                "id": f"sos_{uuid.uuid4().hex[:8]}",
                "device_id": str(payload.get("device_id") or ""),
                "student_id": str(payload.get("student_id") or ""),
                "student_name": str(payload.get("student_name") or "学生"),
                "school": str(payload.get("school") or ""),
                "class_name": str(payload.get("class_name") or ""),
                "primary_phone": str(payload.get("primary_phone") or ""),
                "location": str(payload.get("location") or "校园紧急求助"),
                "created_epoch": now_ep,
            }
            self.sos_alerts.insert(0, alert)
            del self.sos_alerts[20:]
            self._log("SOS_ALERT", f"SOS from {alert['student_name']} ({alert['student_id']}): {alert['location']}")
            return {"ok": True, "server_time": now_ep}

    def set_acl(self, device_id: str, acl_data: dict) -> dict:
        with self._lock:
            acl = self._ensure_acl_locked(device_id)
            for key in ("primary_phone", "student_school", "student_class"):
                if key in acl_data:
                    acl[key] = str(acl_data[key] or "").strip()
            if "family" in acl_data and isinstance(acl_data["family"], list):
                acl["family"] = [
                    {
                        "phone": str(x.get("phone") or "").strip(),
                        "label": str(x.get("label") or "家人").strip(),
                    }
                    for x in acl_data["family"]
                    if str(x.get("phone") or "").strip()
                ]
            if "teachers" in acl_data and isinstance(acl_data["teachers"], list):
                acl["teachers"] = [
                    {
                        "id": str(x.get("id") or "").strip(),
                        "name": str(x.get("name") or "老师").strip(),
                        "phone": str(x.get("phone") or "").strip(),
                        "school": str(x.get("school") or "").strip(),
                        "class_name": str(x.get("class_name") or "").strip(),
                    }
                    for x in acl_data["teachers"]
                ]
            if "admins" in acl_data and isinstance(acl_data["admins"], list):
                acl["admins"] = [
                    {
                        "id": str(x.get("id") or "").strip(),
                        "name": str(x.get("name") or "校管").strip(),
                        "phone": str(x.get("phone") or "").strip(),
                        "school": str(x.get("school") or "").strip(),
                    }
                    for x in acl_data["admins"]
                ]
            self._log("ACL_UPDATED", f"Updated ACL for device {device_id}")
            return {"ok": True, "device_id": device_id, "acl": acl}

    @staticmethod
    def is_sender_authorized(acl: dict, sender_role: str, phone: str,
                             school: str, class_name: str) -> tuple[bool, str]:
        if sender_role == "primary_parent":
            if phone and acl.get("primary_phone") and phone == acl["primary_phone"]:
                return True, "primary_parent_match"
            return False, "primary_phone_mismatch"

        if sender_role == "family":
            if not phone:
                return False, "missing_phone"
            if phone == acl.get("primary_phone"):
                return True, "primary_phone_match"
            for m in acl.get("family", []):
                if m.get("phone") == phone:
                    return True, "family_bound_match"
            return False, "family_phone_not_bound"

        if sender_role == "teacher":
            stu_school = acl.get("student_school", "")
            stu_class = acl.get("student_class", "")
            if stu_school and school != stu_school:
                return False, "teacher_school_mismatch"
            if stu_class and class_name != stu_class:
                return False, "teacher_class_mismatch"
            teachers = acl.get("teachers", [])
            if teachers:
                for t in teachers:
                    if t.get("school") and t["school"] != school:
                        continue
                    if t.get("class_name") and t["class_name"] != class_name:
                        continue
                    if phone and t.get("phone") and t["phone"] != phone:
                        continue
                    return True, "teacher_acl_match"
                return False, "teacher_not_in_acl"
            if school and class_name:
                return True, "teacher_school_class_match"
            return False, "missing_teacher_school_or_class"

        if sender_role == "school_admin":
            stu_school = acl.get("student_school", "")
            if stu_school and school != stu_school:
                return False, "admin_school_mismatch"
            admins = acl.get("admins", [])
            if admins:
                for a in admins:
                    if a.get("school") and a["school"] != school:
                        continue
                    if phone and a.get("phone") and a["phone"] != phone:
                        continue
                    return True, "admin_acl_match"
                return False, "admin_not_in_acl"
            if school:
                return True, "admin_school_match"
            return False, "missing_admin_school"

        return False, "unauthorized_role"

    def client_send(self, payload: dict) -> tuple[int, dict]:
        with self._lock:
            self.purge_expired_locked()
            dev_id = self.resolve_device_id(payload.get("device_id"))
            if not dev_id:
                return 400, {"ok": False, "error": "no_target_device"}

            acl = self._ensure_acl_locked(dev_id)
            sender_role = str(payload.get("sender_role") or "").strip()
            sender_name = str(payload.get("sender_name") or "").strip() or "家长"
            sender_phone = str(payload.get("sender_phone") or "").strip()
            sender_school = str(payload.get("sender_school") or "").strip()
            sender_class = str(payload.get("sender_class") or "").strip()
            text = str(payload.get("text") or "").strip()
            bypass_acl = bool(payload.get("bypass_server_acl", False))

            allowed, reason = self.is_sender_authorized(
                acl, sender_role, sender_phone, sender_school, sender_class
            )
            if not allowed and not bypass_acl:
                self._log(
                    "ACL_REJECT",
                    f"Rejected {sender_role}({sender_name}/{sender_phone}) for {dev_id}: {reason}",
                )
                return 403, {"ok": False, "error": "forbidden", "reason": reason}

            audio_b64 = payload.get("wav_base64")
            adpcm_b64 = payload.get("adpcm_base64")
            if adpcm_b64:
                adpcm = base64.b64decode(adpcm_b64)
                duration_sec = max(1, min(10, int(payload.get("duration_sec") or max(1, len(adpcm) // 4000))))
            elif audio_b64:
                adpcm, duration_sec = wav_bytes_to_adpcm_8k(base64.b64decode(audio_b64))
            else:
                duration_sec = max(1, min(10, int(payload.get("duration_sec") or 3)))
                freq = 523.25 if sender_role in ("primary_parent", "family") else 659.25
                adpcm = synthesize_chime_adpcm_8k(duration_sec=duration_sec, base_freq=freq)

            remote_id = f"m_{uuid.uuid4().hex[:10]}"
            now_ep = self.now()
            msg = {
                "remote_id": remote_id,
                "sender_role": sender_role,
                "sender_name": sender_name,
                "sender_phone": sender_phone,
                "sender_school": sender_school,
                "sender_class": sender_class,
                "text": text or f"{sender_name}发来一条 {duration_sec} 秒语音留言",
                "duration_sec": duration_sec,
                "audio_format": "adpcm_8k",
                "audio": adpcm,
                "created_epoch": now_ep,
            }
            self.to_device.setdefault(dev_id, {})[remote_id] = msg
            note = " (bypass_server_acl)" if (not allowed and bypass_acl) else ""
            self._log(
                "QUEUED_TO_DEVICE",
                f"Queued {remote_id} from {sender_role}({sender_name}) -> {dev_id}, {len(adpcm)}B{note}",
            )
            return 200, {
                "ok": True,
                "device_id": dev_id,
                "remote_id": remote_id,
                "duration_sec": duration_sec,
                "audio_bytes": len(adpcm),
                "server_time": now_ep,
            }

    def client_ack(self, remote_ids: list[str]) -> dict:
        with self._lock:
            deleted = 0
            for rid in remote_ids:
                if rid in self.to_client:
                    del self.to_client[rid]
                    deleted += 1
            if deleted:
                self._log(
                    "CLIENT_ACK_DELETE",
                    f"Client ACKed {deleted} upload(s); purged from server RAM (0 bytes retained)",
                )
            return {
                "ok": True,
                "deleted": deleted,
                "remaining_to_client": len(self.to_client),
            }

    def get_client_audio_wav(self, remote_id: str) -> bytes | None:
        with self._lock:
            m = self.to_client.get(remote_id)
            if not m:
                return None
            rate = 16000 if m["audio_format"] == "adpcm_16k" else 8000
            return adpcm_to_wav_bytes(m["audio"], sample_rate=rate)

    def client_state(self) -> dict:
        with self._lock:
            self.purge_expired_locked()
            to_dev_count = sum(len(b) for b in self.to_device.values())
            to_dev_bytes = sum(len(m["audio"]) for b in self.to_device.values() for m in b.values())
            to_cli_bytes = sum(len(m["audio"]) for m in self.to_client.values())
            pending_device_msgs = [
                {
                    "device_id": dev_id,
                    "remote_id": m["remote_id"],
                    "sender_role": m["sender_role"],
                    "sender_name": m["sender_name"],
                    "text": m["text"],
                    "duration_sec": m["duration_sec"],
                    "audio_bytes": len(m["audio"]),
                    "created_epoch": m["created_epoch"],
                }
                for dev_id, b in self.to_device.items()
                for m in b.values()
            ]
            inbox = [
                {
                    "remote_id": m["remote_id"],
                    "device_id": m["device_id"],
                    "student_id": m["student_id"],
                    "student_name": m["student_name"],
                    "school": m["school"],
                    "class_name": m["class_name"],
                    "duration_sec": m["duration_sec"],
                    "audio_bytes": len(m["audio"]),
                    "text": m["text"],
                    "asr_engine": m["asr_engine"],
                    "created_epoch": m["created_epoch"],
                }
                for m in sorted(self.to_client.values(), key=lambda x: x["created_epoch"], reverse=True)
            ]
            return {
                "ok": True,
                "server_time": self.now(),
                "zero_retention_mode": True,
                "devices": list(self.devices.values()),
                "acls": self.acls,
                "ephemeral_queues": {
                    "to_device_count": to_dev_count,
                    "to_device_bytes": to_dev_bytes,
                    "to_client_count": len(self.to_client),
                    "to_client_bytes": to_cli_bytes,
                },
                "pending_to_device": pending_device_msgs,
                "inbox_from_device": inbox,
                "sos_alerts": list(self.sos_alerts),
                "audit_log": list(self.audit_log),
            }


def make_handler(state: RelayState):
    ui_html = (HERE / "index.html").read_bytes() if (HERE / "index.html").is_file() else b"<h1>Relay Ready</h1>"

    class RelayHandler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def address_string(self) -> str:
            return self.client_address[0]

        def log_message(self, format: str, *args) -> None:
            # Keep logs concise
            sys.stderr.write(f"[relay] {self.address_string()} - {format % args}\n")

        def _send_json(self, code: int, obj: dict) -> None:
            raw = json.dumps(obj, ensure_ascii=False).encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(raw)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(raw)

        def _send_bytes(self, code: int, content_type: str, data: bytes) -> None:
            self.send_response(code)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)

        def _read_body(self) -> bytes:
            length = int(self.headers.get("Content-Length", "0") or "0")
            if length <= 0:
                return b""
            return self.rfile.read(length)

        def _read_json(self) -> dict:
            raw = self._read_body()
            if not raw:
                return {}
            return json.loads(raw.decode("utf-8"))

        def do_GET(self) -> None:
            parsed = urlparse(self.path)
            path = parsed.path
            qs = {k: v[0] for k, v in parse_qs(parsed.query).items()}

            if path == "/" or path == "/index.html":
                self._send_bytes(HTTPStatus.OK, "text/html; charset=utf-8", ui_html)
                return

            if path == "/health":
                self._send_json(HTTPStatus.OK, {"ok": True, "server_time": state.now()})
                return

            if path == "/api/v1/client/state":
                self._send_json(HTTPStatus.OK, state.client_state())
                return

            if path.startswith("/api/v1/device/audio/"):
                rid = path.removeprefix("/api/v1/device/audio/").strip("/")
                audio = state.get_device_audio(rid, qs.get("device_id"))
                if audio is None:
                    self._send_json(HTTPStatus.NOT_FOUND, {"ok": False, "error": "not_found_or_acked"})
                    return
                self._send_bytes(HTTPStatus.OK, "application/octet-stream", audio)
                return

            if path.startswith("/api/v1/client/audio/"):
                rid = path.removeprefix("/api/v1/client/audio/").strip("/")
                wav_data = state.get_client_audio_wav(rid)
                if wav_data is None:
                    self._send_json(HTTPStatus.NOT_FOUND, {"ok": False, "error": "not_found_or_acked"})
                    return
                self._send_bytes(HTTPStatus.OK, "audio/wav", wav_data)
                return

            self._send_json(HTTPStatus.NOT_FOUND, {"ok": False, "error": "unknown_route"})

        def do_POST(self) -> None:
            parsed = urlparse(self.path)
            path = parsed.path
            qs = {k: v[0] for k, v in parse_qs(parsed.query).items()}

            try:
                if path == "/api/v1/device/upload":
                    body = self._read_body()
                    if not body:
                        self._send_json(HTTPStatus.BAD_REQUEST, {"ok": False, "error": "empty_audio"})
                        return
                    res = state.device_upload(qs, body)
                    self._send_json(HTTPStatus.OK, res)
                    return

                payload = self._read_json()

                if path == "/api/v1/device/poll":
                    self._send_json(HTTPStatus.OK, state.device_poll(payload))
                    return

                if path == "/api/v1/device/ack":
                    dev_id = str(payload.get("device_id") or "")
                    rids = [str(x) for x in (payload.get("remote_ids") or [])]
                    self._send_json(HTTPStatus.OK, state.device_ack(dev_id, rids))
                    return

                if path == "/api/v1/device/sos":
                    self._send_json(HTTPStatus.OK, state.device_sos(payload))
                    return

                if path == "/api/v1/client/acl":
                    dev_id = state.resolve_device_id(payload.get("device_id")) or "default"
                    self._send_json(HTTPStatus.OK, state.set_acl(dev_id, payload.get("acl", payload)))
                    return

                if path == "/api/v1/client/send":
                    code, res = state.client_send(payload)
                    self._send_json(code, res)
                    return

                if path == "/api/v1/client/ack":
                    rids = [str(x) for x in (payload.get("remote_ids") or [])]
                    self._send_json(HTTPStatus.OK, state.client_ack(rids))
                    return

                self._send_json(HTTPStatus.NOT_FOUND, {"ok": False, "error": "unknown_route"})
            except json.JSONDecodeError as exc:
                self._send_json(HTTPStatus.BAD_REQUEST, {"ok": False, "error": f"invalid_json: {exc}"})

    return RelayHandler


class UdpDiscoveryResponder(threading.Thread):
    """Responds to ESP32 UDP broadcast discovery packets on port 8788."""

    def __init__(self, http_port: int, discover_port: int = DEFAULT_DISCOVER_PORT,
                 override_url: str | None = None) -> None:
        super().__init__(daemon=True)
        self.http_port = http_port
        self.discover_port = discover_port
        self.override_url = override_url
        self._stop_event = threading.Event()
        self._sock: socket.socket | None = None

    def start_socket(self) -> None:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        sock.settimeout(0.5)
        sock.bind(("0.0.0.0", self.discover_port))
        self._sock = sock

    def run(self) -> None:
        if self._sock is None:
            self.start_socket()
        assert self._sock is not None
        while not self._stop_event.is_set():
            try:
                data, addr = self._sock.recvfrom(512)
            except TimeoutError:
                continue
            except OSError:
                break
            if b"AI_PASSPORT_RELAY_DISCOVER" in data:
                url = self.override_url or f"http://{detect_lan_ip(addr[0])}:{self.http_port}"
                reply = json.dumps({"service": "ai-passport-relay", "url": url}).encode("utf-8")
                try:
                    self._sock.sendto(reply, addr)
                except OSError:
                    pass

    def stop(self) -> None:
        self._stop_event.set()
        if self._sock is not None:
            try:
                self._sock.close()
            except OSError:
                pass


def create_server(host: str = "0.0.0.0", port: int = DEFAULT_HTTP_PORT,
                  state: RelayState | None = None) -> tuple[ThreadingHTTPServer, RelayState]:
    st = state or RelayState()
    httpd = ThreadingHTTPServer((host, port), make_handler(st))
    return httpd, st


def main() -> int:
    parser = argparse.ArgumentParser(description="AI Passport Mini-Relay Server + Simulator")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=DEFAULT_HTTP_PORT)
    parser.add_argument("--discover-port", type=int, default=DEFAULT_DISCOVER_PORT)
    parser.add_argument("--no-udp", action="store_true")
    parser.add_argument("--advertise-url", default=None)
    args = parser.parse_args()

    httpd, _ = create_server(args.host, args.port)
    lan_ip = detect_lan_ip()
    udp = None
    if not args.no_udp:
        udp = UdpDiscoveryResponder(args.port, args.discover_port, args.advertise_url)
        udp.start_socket()
        udp.start()

    print(f"AI Passport Relay Server listening on http://{args.host}:{args.port}", flush=True)
    print(f"LAN URL: http://{lan_ip}:{args.port} (Simulator UI: http://127.0.0.1:{args.port}/)", flush=True)
    if udp:
        print(f"UDP LAN discovery active on port {args.discover_port}", flush=True)

    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        if udp:
            udp.stop()
        httpd.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
