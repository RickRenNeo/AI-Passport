# Xiaozhi AI Integration on the eCard (branch `ecard-xiaozhi`)

<p align="right">
  <a href="xiaozhi-integration.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

This page records how the reference branch [`feature/xiaozhi-tianji`](https://github.com/FoloToy/AI-Passport/tree/feature/xiaozhi-tianji)
connects an AI Passport to Xiaozhi, and the plan we follow on this eCard
firmware. It is an engineering plan, not a claim of completed work.

The agent persona prompt that is configured on the Xiaozhi open platform lives in
[`xiaozhi-persona.zh_CN.md`](xiaozhi-persona.zh_CN.md) (Chinese, ready to paste),
with an English reference in [`xiaozhi-persona.md`](xiaozhi-persona.md).

## 1. How the reference branch does it

| Concern | Implementation in `feature/xiaozhi-tianji` |
| --- | --- |
| Board | `main/boards/folotoy/ai-passport`, ESP32-C3, 8 MB |
| Partition | `partitions/v2/8m.csv`: two 3 MB OTA slots + 2 MB SPIFFS `assets` |
| Framework | Full `xiaozhi-esp32` fork: C++ `Application`, `Board`, `Display`, `AudioService` |
| Wi-Fi | `78/esp-wifi-connect` (`wifi_manager`) SoftAP portal, optional BluFi over NimBLE |
| Protocol | WebSocket (`main/protocols/websocket_protocol.cc`), binary framing: type 0 = Opus audio, type 1 = JSON |
| Audio | Opus, server advertises `sample_rate` (default 24 kHz) and `frame_duration` (default 60 ms) |
| ASR / LLM / TTS | Server-side. JSON events: `listen` → `stt` (text) → `llm` (streamed text) → `tts` (Opus frames) → `tts stop` |
| Wake word | `esp-sr` models; the AI Passport build sets `CONFIG_WAKE_WORD_DISABLED=y` |
| OTA / activation | `ota.cc` + server-issued activation code, MQTT or WebSocket transport |

Two consequences matter for us:

1. **Both OTA slots are 3 MB.** The eCard UI alone ships 3 MB of CJK bitmap fonts, so
   the eCard cannot live inside a 3 MB OTA slot without a font strategy change.
2. **Opus plus `esp-sr` plus the xiaozhi framework is roughly 1.5-2.5 MB of Flash**,
   before fonts or card data.

## 2. What this branch already changes

* Branch `ecard-xiaozhi` created from the finished e-card feature branch.
* **All offline pronunciation assets removed** (`assets/speech/`, `ecard_speech.c`),
  freeing about 1.0 MB of Flash; speech is now expected from server TTS.
* **First-run provisioning** (phone SoftAP portal, no keyboard needed):
  * AP `AI-Passport-XXXX`, password `passport1234`, captive portal at `192.168.4.1`;
  * the OS captive-portal probe is answered with a redirect, so the page opens
    automatically after the phone joins the AP;
  * two-step flow: Wi-Fi first — the device scans nearby networks and the phone
    picks one from the list (manual entry still works); once the STA link is up the
    page auto-redirects to the identity form — student: 8-digit education ID,
    school, class, name, bound parent phone; teacher: staff ID, school, name, bound phone;
  * stored in NVS (`ecard_setup.c`), validated by `ecard_setup_validate.c`, applied to
    the badge profile on the next boot;
* **Default network**: reads NVS-provisioned credentials (with an empty compile-time fallback in `main/ecard_setup.h`); fast 5/10/20/30 s retries after a drop, then a 2-minute steady interval
  (`ecard_netlink.c`); three consecutive failures fall back to the portal.
* **Device reset page**: Device Info → DOWN → confirm → erase Wi-Fi and identity, reboot.
* **Time sync**: SNTP (`ntp.aliyun.com`, `cn.pool.ntp.org`) with fixed UTC+8
  (`ecard_time.c`); the status bar uses real time once synced.

## 3. Current implementation status

As of the `ecard-xiaozhi` build on 2026-11-10:

* **Activation/OTA works.** The 400 `Invalid client ID` seen earlier came from
  sending the MAC as `Client-Id`; the device now generates and persists a UUID v4
  (`client_id` in NVS) and the server returns HTTP 200 plus the WebSocket URL and
  token. HTTPS needs the certificate bundle attached explicitly.
* **WebSocket transport works.** `ecard_xiaozhi.c` connects to
  `wss://api.tenclass.net/xiaozhi/v1/`, sends `hello` (Opus, 16 kHz mono, 60 ms),
  receives the server `hello` (24 kHz / 60 ms), and handles JSON events
  (`stt`, `llm`, `tts`) plus raw-Opus binary frames.
* **The session is on demand.** Boot no longer connects to Xiaozhi: keeping TLS
  and the Opus decoder open squeezed the voice-recording buffer down to about
  2 seconds. `ecard_xiaozhi_session_start()` is called only from explicit AI
  entry points (quiz explanation, poem/word reading); `session_stop()` closes the
  socket and frees TLS/Opus memory. Home recording never triggers Xiaozhi.
* **ASR upload path exists but is not attached to the home recording button.**
  `listen start/stop` and 60 ms Opus frames remain available for the dedicated
  AI entry points.
* **TTS playback path is wired.** Opus frames are decoded in a dedicated task,
  resampled 24 kHz → 16 kHz, buffered in a 192 ms ring, and pulled by the BSP
  audio task. RAM is the constraint: the PCM scratch and ring buffers live in
  static storage, and only the outbound TLS record was trimmed to 2 KB.
  The decode task needs a **10 KB stack**: 6 KB overflowed the stack protector
  (`Guru Meditation … Stack protection fault`, task `xz_opus`) on the first TTS
  frame, which rebooted the device. The ESP-IDF RISC-V port defines
  `portSTACK_TYPE` as `uint8_t`, so stack depth is measured in **bytes**:
  declaring 2560 produced a 2.5 KB stack and rebooted on the first TTS frame.
  The inbound TLS record must stay at 4 KB: the activation endpoint sends the
  whole certificate chain as one 3974-byte record, and 2 KB makes
  `mbedtls_ssl_handshake` return `-0x7100` (BAD_INPUT_DATA), breaking both
  activation and the WebSocket handshake.
  Every 32 decoded frames the task logs its remaining stack head-room, and the
  connect handler logs free heap after the handshake.
* **All three AI entries are wired.** Quiz answer submit, poem flip, and word
  flip start the session and send a compact prompt; long-press UP starts a
  manual `listen` turn so the student can ask a follow-up question. Leaving the
  AI pages calls `session_stop()`.
* **Control traffic never blocks the UI.** JSON messages (`hello`, `listen`) are
  handed to a dedicated `xz_tx` task through a small queue; the LVGL thread no
  longer writes to the socket. The task is created at session start, before
  OTA, so its 3.5 KB stack is not requested at the moment the TLS handshake
  completes (that timing dropped post-handshake free heap to 8.2 KB and the
  socket then stalled). One write is capped at 300 ms, a failed prompt is retried
  once per second, and the pending prompt is latest-only and gated on the server
  `session_id`.
* **A new question aborts the previous answer.** Verified against the live
  server: `{"type":"abort"}` stops the running TTS immediately. Before sending
  the next `detect`/`listen` the device aborts the old turn and clears its PCM
  ring, so the server never streams two answers at once.
* **Microphone frames are encoded off the audio task.** The Opus encoder needs
  far more than the 4 KB audio-task stack: opening it there rebooted the device
  on the first follow-up long-press (`Stack protection fault`, task
  `ecard_audio`). The audio task now only fills 60 ms frames and hands them to a
  dedicated 8 KB `xz_enc` task created for the session.
* **Memory budget is the whole story.** TLS 1.3 + Opus need roughly 40 KB of
  free heap; when the WebSocket connect step logged only 8 KB, mbedTLS failed
  with `mbedtls_ssl_handshake returned -0x2880` and every AI entry stayed in
  “connecting”. Three changes buy the margin: the LVGL draw buffer was cut from
  40 to 24 lines (~7.7 KB), the microphone encoder task (11.8 KB) exists only
  while the student holds UP, and recordings are capped at 10 s (40 KB) and
  released before an AI session.
* **The Opus encoder replaces the decoder.** While the student holds UP the
  decoder and its task are closed so the encode task stack and queue fit; after
  `listen stop` the encoder is freed and the decoder reopens on the next TTS
  frame. The encoder must be closed together with the encode task; closing only
  the task leaves the decoder opening beside it, which repeatedly fails with
  `-2` and produces no sound.
* **The encoder's contiguous block comes from a static arena.** `esp_audio_codec`
  allocates through the weak `media_lib_module_malloc/calloc/realloc/free`
  symbols. `main/ecard_opus_arena.c` overrides them with strong definitions:
  only inside the `ecard_opus_arena_begin()` → `esp_audio_enc_open()` →
  `ecard_opus_arena_end()` window, and only for requests ≥12 KB, it serves the
  block from a 25 KB static arena; everything else still uses the system heap.
  After TLS and Wi-Fi buffers fragment the heap the largest free block is
  ~18 KB, so plain `malloc` can never satisfy the encoder
  (`OPUS_ALLOC_FAIL(-7)`). The 16 kHz / 60 ms / VOIP encoder measured
  **24524 bytes** on device; the arena previously declared 20 KB and silently
  returned an undersized block, so libopus wrote past the array into the
  adjacent LVGL memory pool and the device rebooted in `lv_tlsf`. Requests
  larger than the arena now fail cleanly instead of corrupting memory.
* **The encoder cannot be pre-opened before TLS.** Measured: keeping the encoder
  (24.5 KB), the decoder and the WS client resident left only 8 KB before
  connecting, so mbedTLS failed with
  `mbedtls_ssl_handshake returned -0x2880`; the resident encoder also starved
  the decoder, and poem/word TTS played nothing at all.
* **Wi-Fi dynamic buffers are the largest reclaimable block.** The device keeps
  exactly one Xiaozhi WebSocket, so the default 32+32 dynamic TX/RX buffers
  (~50 KB) are wasteful. `sdkconfig.defaults` now uses 8+8, 8 static RX and 3
  management buffers, leaving room for TLS and Opus.
* **The server decodes device audio as 60 ms frames.** `xiaozhi-server`'s
  `_decode_opus_packet` always calls `decoder.decode(packet, 960)`, so the
  encoder may only emit 60 ms frames (960 samples at 16 kHz); 20/10 ms frames
  are decoded as garbage. Do not shorten the frame to save memory.
* **A rejected long prompt needs `abort` first.** Against the live server, once
  it sends the `Detect is only for wake words...` alert, later short `detect`
  messages in the same session are silently dropped. The device now sends
  `abort` to reset that state before sending the fallback prompt.
* **The recording buffer is allocated only while recording.** Keeping the
  10-second buffer resident for the whole run cost 40 KB and left ~10 KB for
  TLS/Opus: `ESP_OPUS_DEC` then failed (`ret -7`, no audio) and the server
  `hello` never arrived. The clip is freed when an AI session starts and
  re-allocated on the next recording, which normally still yields 10 s.
* **The Opus decoder is opened before TLS.** Opening it after the handshake
  leaves too little contiguous heap, so no TTS plays at all.
* **Reconnect hardening.** If the WebSocket is up but the server `hello` never
  arrives, the TX task resends `hello` after 5 s and then every 3 s (four
  attempts) and logs each retry; disconnects and server-side closes now reset
  `tts_active` and the PCM ring so the UI cannot stay stuck in “speaking”.
* **Follow-up listening has explicit feedback.** On quiz/poem/word pages,
  long-press UP opens a dedicated AI listening overlay: it states that Xiaozhi
  is listening, asks the student to release UP to send, and shows the
  10-second limit. The device streams Opus frames and sends `listen stop` on
  release; the 10-second cap stops and sends automatically. If the session is
  still connecting, the page hint says so instead of ignoring the press.
  Entering any AI page starts the OTA/WebSocket session in the background.
* **Navigation and unread state.** Menu rows move on the button-press edge (the
  following `CLICK` is consumed), so holding a key no longer feels delayed; the
  recess poll no longer fabricates an unread message.
* **Prompts have two measured limits.** Each limit was probed against the
  official server: `listen`/`detect` text containing Chinese passes at 16
  characters and is rejected at 18; pure ASCII passes at 30 bytes and is
  rejected at 36. Over the limit the server answers with
  `Detect is only for wake words, do not send long texts.` and drops the whole
  turn. Prompts are therefore built from card data in `main/ecard_ai_prompt.c`:
  quizzes use their ≤16-character `ai_prompt` (falling back to the generic
  explain prompt), poems send the two card lines joined by `|` (≤15 characters),
  and words go out in two turns — `read:word,meaning` (≤16 characters, with the
  meaning dropped rather than truncated when it does not fit) followed
  automatically by the card's own example sentence (pure ASCII ≤30 bytes) once
  the first answer finishes. The second turn relies on the previous turn's
  context so Xiaozhi reads and translates the real card sentence instead of
  inventing one. `tests/test_ecard_ai_prompt.c` asserts every quiz question,
  poem pair and word card fits.
* **Prompts wait for the session id.** `detect` is only sent after the server
  `hello` supplies `session_id`; sending earlier raced with the handshake and
  silently dropped the question.
* **Local messages replay the real recording.** The recorded clip is no longer
  released when playback stops; it stays until the next recording starts, and
  only the message that owns it (`s_clip_message_id`) uses it. Messages received
  from the network still play the built-in demo clip because they carry no
  audio payload yet.
* Still missing: transcript/answer text in the eCard UI, and network voice
  messages still use the built-in demo clip until the ASR/TTS turn is confirmed
  on hardware.

## 4. Where each Xiaozhi capability lands

| Requirement | eCard integration point | Server message |
| --- | --- | --- |
| Wi-Fi configuration | Local SoftAP portal (already implemented); can later reuse `wifi_manager` | none |
| Voice message → text | `ecard_voice.c` records; upload Opus; show transcript while playing | `listen` / `stt` / `tts` |
| Quiz explanation, multi-turn | `ecard_quiz.c` asks for an explanation; follow-up turns keep context | `llm` text stream, `mcp` tools |
| Poem / word reading | Card flip requests TTS for the visible text | `tts` Opus frames |
| Time calibration | Not provided by Xiaozhi protocol → SNTP as implemented | none |

## 5. Flash budget

| Item | Bytes |
| --- | ---: |
| eCard app after removing speech assets | ~4.0 MB |
| CJK bitmap fonts (9/12/16/22/48 px) | ~2.3 MB |
| Opus encoder + decoder (`esp_opus`) | ~0.3 MB |
| WebSocket + JSON + NVS settings | ~0.2 MB |
| TLS (mbedTLS, already linked) | ~0.3 MB |
| Remaining for cards and head-room | ~1.4 MB |

Font work changed the arithmetic: 2 bpp plus a literal subset was 346 KB, but
user-entered names need broader coverage, so 12/16/22 px now carry GB2312
level-1. The font payload is ~1.0 MB and the application 2.72 MB. This branch
therefore switched `partitions.csv` to dual OTA:

```text
nvs      0x9000   0x6000   (offset and size unchanged, settings survive)
phy_init 0xf000   0x1000
otadata  0x10000  0x2000
ota_0    0x20000  0x3f0000  (4.13 MB)
ota_1    0x410000 0x3f0000  (4.13 MB)
```

`ota_0` holds the running image; OTA updates write `ota_1` and flip `otadata`,
so a failed update can roll back. The one-time switch from the old single-factory
table still needs a USB flash of the merged image, because the partition table
itself changes.

## 6. Open decisions

1. **Server endpoint and activation**: the Xiaozhi WebSocket URL and how the device is
   bound (activation code vs pre-shared token). The reference branch obtains them
   through its OTA/activation flow; we need the concrete values for this deployment.
2. **Audio path**: Xiaozhi expects Opus. We currently record/play raw 16-bit PCM, so the
   integration needs an Opus encoder for upload and a decoder for playback, plus
   resampling to the server's advertised rate.
3. **OTA transport**: the partition is ready for dual OTA; the update client still has to be written.
