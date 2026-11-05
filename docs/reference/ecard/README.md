<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# AI Passport Electronic Campus Card (eCard)

A dedicated, safe, distraction-free, and low-power wearable campus card / identity badge designed for K12 schools and universities.

---

## 1. Core Features and Design Principles

1. **NFC Identity & Display (P0)**:
   - **Large Standard Portrait Photo**: Standard 5:7 aspect ratio (125 × 175 px) on the right side of the screen;
   - **Vertical Rotated Barcode**: Code128 standard barcode aligned vertically on the left side (80 × 150 px);
   - **Badge Theme System**: Plan A preset capsule themes (Vibrant Blue for students, Gold for faculty), compatible with Plan B NVS customizations;
   - **Mutual Exclusion Layout**: Displays large Student Name & Class + Badge Capsule during normal status; dynamically swaps into a prominent notification card when an unread voice message arrives.
2. **Two-Way Voice Intercom & SOS Emergency Alert (P0)**:
   - **Long Press OK (≥1s)**: Starts voice recording (up to 30s countdown without needing to keep holding the button); streams to Flash and uploads on demand;
   - **Short Press OK**: Plays incoming voice messages or stops current audio playback;
   - **Short Press UP**: Manually queries and fetches new voice messages from server;
   - **Long Press DOWN (≥3s)**: Triggers silent SOS emergency alert with 15s environmental audio clip upload;
   - **Recess Time Detection**: Built-in school schedule table; automatically fetches new messages during recess intervals only; keeps WiFi RF powered off during classes;
   - **2-Minute Screen Sleep**: Automatically dims backlight after 2 minutes of inactivity; instantly wakes up on any key press.

---

## 2. Interaction Matrix

| Gesture | Current State | Action |
| :--- | :--- | :--- |
| **Long Press OK (≥1s)** | Idle | Starts 30s voice recording modal (hands-free) |
| **Short Press OK** | Recording | Finishes recording early and uploads |
| **Short Press OK** | Playing Voice | Stops voice playback early |
| **Short Press OK** | Idle with Unread Msg | Starts playback of unread voice message |
| **Short Press UP** | Idle | Manually synchronizes messages over WiFi |
| **Long Press DOWN (≥3s)** | Any | Triggers silent SOS emergency alert |
| **Any Key** | Dimmed/Asleep | Instantly wakes screen and restores 100% brightness |

---

## 3. Architecture Modules

* [`ecard_app.h`](../../../main/ecard_app.h) / [`ecard_app.c`](../../../main/ecard_app.c): Global coordinator, key dispatcher, and recess schedule loop
* [`ecard_ui.h`](../../../main/ecard_ui.h) / [`ecard_ui.c`](../../../main/ecard_ui.c): 240×320 LVGL portrait UI, mutual exclusion container, and modals
* [`ecard_barcode.h`](../../../main/ecard_barcode.h) / [`ecard_barcode.c`](../../../main/ecard_barcode.c): Code128B 90-degree rotated vertical barcode generator
* [`ecard_profile.h`](../../../main/ecard_profile.h) / [`ecard_profile.c`](../../../main/ecard_profile.c): Identity profile data model and Plan A/B themes
* [`ecard_voice.h`](../../../main/ecard_voice.h) / [`ecard_voice.c`](../../../main/ecard_voice.c): IMA-ADPCM streaming codec and 30s record buffer manager
* [`ecard_network.h`](../../../main/ecard_network.h) / [`ecard_network.c`](../../../main/ecard_network.c): Preset campus WiFi manager and recess schedule checker

---

## 4. Host Verification

All components are integrated into the automated repository test suite:

```bash
# Run all repository checks and host-based unit tests
./tools/validate.sh --static
```
