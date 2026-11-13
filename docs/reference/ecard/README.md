<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# AI Passport Electronic Campus Card (eCard)

A dedicated, safe, distraction-free, and low-power wearable campus card / identity badge designed for K12 schools and universities.

---

## 1. Core Features and Design Principles

1. **NFC Identity & Display (P0)**:
   - **Badge Portrait**: a 130 × 184 RGB565 portrait inside a 134 × 188 white frame on the home badge, which is itself the complete student card (there is no separate credential page);
   - **Barcode**: Code128C for even-length numeric IDs (Code128B fallback), drawn directly by LVGL draw callbacks vertically beside the home portrait, without an RGB565 image buffer;
   - **Badge Theme System**: Plan A preset capsule themes (Vibrant Blue for students, Gold for faculty), compatible with Plan B NVS customizations;
   - **Mutual Exclusion Layout**: Displays large Student Name & Class + Badge Capsule during normal status; swaps into a prominent amber notification card when an unread voice message arrives.
2. **Seven-Page Interface**:
   - **Home**: centered school name with a gold rule, a left vertical barcode card, a large portrait, a floating name/class line and badge capsule, status bar, and the mutual-exclusion unread card;
   - **Quick Menu**: Voice Messages / Quiz / Poems / Words / Pomodoro / Device Info, selected with UP/DOWN; page titles carry their one-line description inline to save vertical space; Poems and Words each open a grade picker for grades 1-6 first;
   - **Voice Messages**: a scrolling list of the past 24 hours, newest first, covering both received and sent clips; unread entries carry a blue dot, read entries can be replayed, UP/DOWN select and OK plays, long-press UP syncs, long-press OK returns to the menu;
   - **Quiz**: a 10-question offline practice set with A/B/C options, immediate right/wrong feedback, and a final score; long prompts/options automatically drop one font size and the option rows expand, so three-line questions and two-line answers stay fully visible; teacher-side question push and class statistics remain a later backend milestone;
   - **Device Info**: battery, network, firmware, storage, mode, and the SOS reminder.
3. **First-Run Provisioning & Device Reset (P0)**:
   - **Phone provisioning**: on first boot the device opens a SoftAP (`AI-Passport-XXXX` / `passport1234`) and a captive portal that auto-opens on the phone. The flow is two-step: first the Wi-Fi form (it scans nearby networks and lets the phone pick one, with manual entry as fallback), then — once the STA link is up — the page auto-redirects to the identity form (student: 8-digit education ID, school, class, name, bound parent phone; teacher: staff ID, school, name, bound phone). The device only counts as provisioned after both steps;
   - **Default network**: reads NVS-provisioned credentials (with an empty compile-time fallback in `main/ecard_setup.h`), retries quickly right after a drop (5/10/20/30s) and then settles on the 2-minute interval; after three consecutive failures (or when unprovisioned) the provisioning portal stays open;
   - **Device reset**: Device Info → DOWN opens a confirmation page; OK erases the stored Wi-Fi and identity and reboots. Study progress and deck state are not touched;
   - **Time sync**: after obtaining an IP the device runs SNTP (`ntp.aliyun.com`, `cn.pool.ntp.org`) and switches to China Standard Time (UTC+8).
4. **Two-Way Voice Intercom & SOS Emergency Alert (P0)**:
   - **Long Press OK**: Starts real microphone recording. A 40 KB buffer is reserved at boot for 10 seconds at 8 kHz IMA-ADPCM (4 KB/s, played back interpolated to 16 kHz); the logical limit is 15 s when extra heap is available. On release the clip is uploaded through the network stub and immediately played back locally. This path never talks to Xiaozhi;
   - **Short Press OK**: Plays an unread voice message, stops playback, opens the menu from Home, or enters the selected menu item;
   - **Short Press UP**: Manually queries and fetches new voice messages from the server; a message ID is remembered so a message that was already read never returns as unread;
   - **Long Press DOWN (≥3s)**: Enters a 3-second SOS confirmation screen; keep holding DOWN to send, release early to cancel;
   - **Recess Time Detection**: Built-in school schedule table; automatically fetches new messages during recess intervals only; keeps WiFi RF powered off during classes;
   - **2-Minute Screen Sleep**: Automatically dims the backlight after 2 minutes without input; any key instantly wakes the screen.

The Chinese UI uses five generated Noto Sans CJK SC subset fonts (9/12/16/22/48 px). Every text widget selects its font explicitly; see [`assets/README.md`](../../../assets/README.md#ecard-ui-fonts) for the pinned source, license, coverage, and regeneration command.

---

## 2. Interaction Matrix

| Gesture | Page / State | Action |
| :--- | :--- | :--- |
| Short Press OK | Home, no unread | Opens the Quick Menu |
| Short Press OK | Home, unread | Plays the unread voice message |
| Short Press OK | Quick Menu | Enters the selected item |
| Short Press OK | Device | Returns to the Quick Menu |
| Short Press OK | Messages | Plays the selected message (read entries replay too) |
| Short Press OK | Messages, empty | Returns to the Quick Menu |
| Short Press OK | Quiz, unanswered | Submits the selected A/B/C answer |
| Short Press OK | Quiz, answered | Advances to the next question |
| Short Press OK | Quiz, finished | Returns to the Quick Menu |
| Short Press OK | Poems / Words, front | Flips the card, shows the explanation, and speaks the back side |
| Short Press OK | Poems / Words, finished | Returns to the Quick Menu |
| Short Press UP | Pomodoro, idle / done | Focus length +5 min (max 50) |
| Short Press DOWN | Pomodoro, idle / done | Focus length -5 min (min 5) |
| Short Press OK | Pomodoro, idle / done | Starts the focus round |
| Short Press OK | Pomodoro, running / paused | Pauses or resumes |
| Short Press DOWN | Pomodoro, running / paused | Stops the current round |
| Short Press DOWN | Poems / Words, flipped | Marks the card known, draws and speaks the next card |
| Short Press UP | Poems / Words, flipped | Requeues the card to the end of the round |
| Short Press OK | Recording | Finishes recording early and uploads |
| Short Press OK | Playing | Stops playback and marks the message read |
| Short Press OK | SOS sent | Returns to Home |
| Long Press OK | Home | Starts the 15s recording overlay |
| Long Press OK | Quick Menu | Returns to Home |
| Long Press OK | Messages / Quiz / Poems / Words / Pomodoro / Device | Returns to the Quick Menu |
| Short Press UP | Home | Manually synchronizes messages |
| Short Press UP | Messages | Selects the previous (newer) entry |
| Short Press DOWN | Messages | Selects the next (older) entry |
| Long Press UP | Messages | Manually synchronizes messages |
| Short Press UP | Quick Menu / Quiz | Moves the selection up (wraps) |
| Long Press UP | Device Info, offline | Retries the Wi-Fi connection immediately |
| Short Press DOWN | Quick Menu / Quiz | Moves the selection down (wraps) |
| Long Press UP | Quiz / Poem / Word | Asks Xiaozhi a follow-up question (hold to speak, release to send) |
| Long Press DOWN | Any page | Enters the 3s SOS confirmation overlay |
| Release DOWN | SOS confirmation | Cancels the alert |
| Short Press OK | SOS sent | Acknowledges and returns to Home |
| Any Key | Dimmed screen | Restores 100% backlight and resets the 2-minute timer |

---

## 3. Architecture Modules

* [`ecard_app.h`](../../../main/ecard_app.h) / [`ecard_app.c`](../../../main/ecard_app.c): Global coordinator, page navigation, SOS confirmation state machine, and recess schedule loop
* [`ecard_ui.h`](../../../main/ecard_ui.h) / [`ecard_ui.c`](../../../main/ecard_ui.c): 240×320 LVGL portrait UI, eleven lazy-created pages, four overlays, barcode draw callbacks, and the idle-dimming timer
* [`ecard_quiz.h`](../../../main/ecard_quiz.h) / [`ecard_quiz.c`](../../../main/ecard_quiz.c): offline 10-question bank plus the answer/score state machine
* [`ecard_setup.h`](../../../main/ecard_setup.h) / [`ecard_setup.c`](../../../main/ecard_setup.c) / [`ecard_setup_validate.c`](../../../main/ecard_setup_validate.c): NVS-backed first-run configuration, identity validation, and profile application
* [`ecard_netlink.h`](../../../main/ecard_netlink.h) / [`ecard_netlink.c`](../../../main/ecard_netlink.c): station Wi-Fi with fast retry after a drop (5/10/20/30s) that backs off to the 2-minute steady interval, scan-aware disconnects, and portal fallback
* [`ecard_portal.h`](../../../main/ecard_portal.h) / [`ecard_portal.c`](../../../main/ecard_portal.c): SoftAP + HTTP form that writes Wi-Fi and identity, then reboots
* [`ecard_time.h`](../../../main/ecard_time.h) / [`ecard_time.c`](../../../main/ecard_time.c): SNTP client with a fixed UTC+8 timezone
* [`ecard_pomodoro.h`](../../../main/ecard_pomodoro.h) / [`ecard_pomodoro.c`](../../../main/ecard_pomodoro.c): study timer state machine with pause/resume, auto transitions, and the four-round long break
* [`ecard_cards.h`](../../../main/ecard_cards.h) / [`ecard_cards.c`](../../../main/ecard_cards.c): generated grade 1-6 poem and word tables, plus the flat word-audio index map
* [`ecard_flashcard.h`](../../../main/ecard_flashcard.h) / [`ecard_flashcard.c`](../../../main/ecard_flashcard.c): deck state machine over any year-group card set, with the pending queue and unknown-requeue rule
* [`ecard_barcode.h`](../../../main/ecard_barcode.h) / [`ecard_barcode.c`](../../../main/ecard_barcode.c): Code128C/Code128B auto encoder, horizontal draw callback, and legacy RGB565 vertical renderer
* [`ecard_photo.h`](../../../main/ecard_photo.h) / [`assets/images/ecard_portrait.c`](../../../assets/images/ecard_portrait.c): Generated RGB565 portrait descriptor for the home badge
* [`ecard_profile.h`](../../../main/ecard_profile.h) / [`ecard_profile.c`](../../../main/ecard_profile.c): Identity profile, validity period, and Plan A/B themes
* [`ecard_voice.h`](../../../main/ecard_voice.h) / [`ecard_voice.c`](../../../main/ecard_voice.c): IMA-ADPCM streaming codec, heap-backed 15s record buffer, peak-level metering, and the local playback decoder. Network messages carry no audio payload yet, so playback falls back to the built-in demo clip in [`assets/sounds/ecard_demo_voice.c`](../../../assets/sounds/ecard_demo_voice.c)
* [`ecard_messages.h`](../../../main/ecard_messages.h) / [`ecard_messages.c`](../../../main/ecard_messages.c): Rolling 24-hour message store with received/sent direction, unread flags, monotonic age ordering, ID deduplication, and relative-time labels
* [`ecard_network.h`](../../../main/ecard_network.h) / [`ecard_network.c`](../../../main/ecard_network.c): Preset campus WiFi manager and recess schedule checker
* [`assets/fonts/`](../../../assets/fonts/): Generated LVGL CJK bitmap fonts and the Noto Sans CJK SC license
* [`tools/generate_ecard_fonts.py`](../../../tools/generate_ecard_fonts.py): Reproducible font-subset generator
* [`tools/generate_ecard_photo.py`](../../../tools/generate_ecard_photo.py): Portrait crop, enhancement, and RGB565 descriptor generator
* [`tests/test_ecard_fonts.py`](../../../tests/test_ecard_fonts.py): Per-font glyph coverage, negative probe, and build-integration gate

---

## 4. Host Verification

All components are integrated into the automated repository test suite:

```bash
# Run all repository checks and host-based unit tests
./tools/validate.sh --static
```
