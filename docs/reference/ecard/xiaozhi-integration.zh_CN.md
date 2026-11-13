# eCard 接入小智 AI（分支 `ecard-xiaozhi`）

<p align="right">
  <strong>简体中文</strong> · <a href="xiaozhi-integration.md">English</a>
</p>

本文记录参考分支 [`feature/xiaozhi-tianji`](https://github.com/FoloToy/AI-Passport/tree/feature/xiaozhi-tianji)
如何把 AI Passport 接入小智，以及本 eCard 固件采用的落地方案。这是工程计划，不是已完成功能的声明。

小智开放平台使用的角色提示词见
[`xiaozhi-persona.zh_CN.md`](xiaozhi-persona.zh_CN.md)（中文，可直接粘贴），
英文参考为 [`xiaozhi-persona.md`](xiaozhi-persona.md)。

## 一、参考分支的做法

| 关注点 | `feature/xiaozhi-tianji` 的实现 |
| --- | --- |
| 板级 | `main/boards/folotoy/ai-passport`，ESP32-C3，8 MB |
| 分区 | `partitions/v2/8m.csv`：两个 3 MB OTA 槽 + 2 MB SPIFFS `assets` |
| 框架 | 完整 `xiaozhi-esp32` 分支：C++ `Application` / `Board` / `Display` / `AudioService` |
| Wi-Fi | `78/esp-wifi-connect`（`wifi_manager`）SoftAP 配网页，可选 NimBLE BluFi |
| 协议 | WebSocket（`main/protocols/websocket_protocol.cc`），二进制帧：type 0 = Opus 音频，type 1 = JSON |
| 音频 | Opus，服务端下发 `sample_rate`（默认 24 kHz）与 `frame_duration`（默认 60 ms） |
| ASR / LLM / TTS | 全部在服务端。JSON 事件：`listen` → `stt`（文字）→ `llm`（流式文字）→ `tts`（Opus 帧）→ `tts stop` |
| 唤醒词 | `esp-sr` 模型；AI Passport 配置里 `CONFIG_WAKE_WORD_DISABLED=y` |
| OTA / 激活 | `ota.cc` + 服务端下发激活码，MQTT 或 WebSocket 传输 |

对我们有两点直接约束：

1. **两个 OTA 槽都只有 3 MB**。eCard 光中文字库就约 2.3 MB，不改字库策略就放不进 3 MB 槽。
2. **Opus + `esp-sr` + 小智框架约 1.5–2.5 MB**，这还没算字库和卡表。

## 二、本分支已完成的改动

* 从已完成的 e-card 分支拉出 `ecard-xiaozhi`。
* **删除全部离线发音资源**（`assets/speech/`、`ecard_speech.c`），释放约 1.0 MB Flash；
  发音改由服务端 TTS 提供。
* **首次配网引导**（手机 SoftAP 表单，无需键盘）：
  * 热点 `AI-Passport-XXXX`，密码 `passport1234`，强制门户地址 `192.168.4.1`；
  * 手机连上热点后，系统探测请求会收到 302，自动弹出配网页，无需手动输入网址；
  * 两步流程：先填 Wi-Fi——设备自动扫描附近网络，手机从列表选择（也可手动输入）；
    连上后页面自动跳转身份页——学生：8 位教育 ID、学校、班级、姓名、绑定家长手机号；
    教师：工号、学校、姓名、绑定手机号；
  * 存入 NVS（`ecard_setup.c`），由 `ecard_setup_validate.c` 校验，重启后写入档案。
* **默认网络**：优先读取 NVS 已配网凭据（`main/ecard_setup.h` 留空回退默认值），断线后先 5/10/20/30 秒快速重试，随后每 2 分钟一次（`ecard_netlink.c`）；
  连续失败 3 次自动回退配网页。
* **设备重置页**：设备信息 → DOWN → 确认 → 清除 Wi-Fi 与身份并重启。
* **时间校准**：SNTP（`ntp.aliyun.com`、`cn.pool.ntp.org`），固定 UTC+8
  （`ecard_time.c`）；同步后状态栏使用真实时间。

## 三、当前实现状态

截至 2026-11-10 的 `ecard-xiaozhi` 构建：

* **OTA 激活已打通。** 之前的 `400 Invalid client ID` 是因为把 MAC 当作
  `Client-Id`；现在设备生成并持久化 UUID v4（NVS 里的 `client_id`），服务端返回
  200 与 WebSocket 地址、token。HTTPS 必须显式挂证书包。
* **WebSocket 传输已打通。** 连接 `wss://api.tenclass.net/xiaozhi/v1/`，发送
  `hello`（Opus / 16 kHz 单声道 / 60 ms），收到服务端 `hello`（24 kHz / 60 ms），
  可解析 `stt`、`llm`、`tts` JSON 事件与裸 Opus 二进制帧。
* **会话改为按需建立。** 开机不再连接小智：TLS 与 Opus 解码器常驻会把录音缓冲
  挤到约 2 秒。`ecard_xiaozhi_session_start()` 只由明确的 AI 入口调用（答题讲解、
  古诗/单词朗读），`session_stop()` 关闭连接并释放 TLS/Opus 内存。首页长按录音
  不会触发小智。
* **ASR 上传链路保留但不再挂在首页录音键上。** `listen start/stop` 与 60 ms Opus
  帧留给上述专用入口使用。
* **TTS 播放链路已接好。** Opus 帧在独立任务解码，24 kHz 重采样到 16 kHz，
  进入 192 ms 环形缓冲，由 BSP 音频任务取走播放。内存是主要约束：PCM 暂存和
  环形缓冲改到静态区，只把**出站** TLS 记录缩到 2 KB。
  解码任务需要 **10 KB 栈**：6 KB 会在第一帧 TTS 上触发栈保护
  （`Guru Meditation … Stack protection fault`，任务 `xz_opus`）并重启设备。
  注意 ESP-IDF 的 RISC-V 端口 `portSTACK_TYPE` 是 `uint8_t`，栈深按**字节**
  计：写成 2560 只会得到 2.5 KB 栈，第一帧 TTS 就重启。
  **入站** TLS 记录必须保持 4 KB：激活服务端把整条证书链放在一条 3974 字节的
  记录里，压到 2 KB 会让 `mbedtls_ssl_handshake` 返回 `-0x7100`
  （BAD_INPUT_DATA），激活和 WebSocket 握手都会失败。
  解码任务每 32 帧打印一次栈余量，连接成功时打印握手后的剩余堆。
* **三个 AI 入口全部接好。** 答题提交、古诗翻面、单词翻面都会建立会话并发送
  精简提示词；长按 UP 进入手动 `listen`，学生可以追问。离开 AI 页面即调用
  `session_stop()`。
* **控制消息不再阻塞界面。** `hello`、`listen` 等 JSON 统一交给独立的 `xz_tx`
  任务，经小队列发送，LVGL 线程不再直接写 socket。该任务在会话开始、OTA 之前
  创建：若等到 TLS 握手完成再申请 3.5 KB 栈，握手后剩余堆会掉到 8.2 KB，随后
  socket 直接卡死 10 秒。单次写超时 300 ms，提问失败每秒重试一次；待发提问只
  保留最新一条，并等服务端 `session_id` 到达后再发。
* **新提问会先中止上一轮回答。** 实机验证 `{"type":"abort"}` 能立刻停止正在播放
  的 TTS。发下一条 `detect`/`listen` 之前先 abort 并清空 PCM 缓冲，避免服务端
  同时推两段回答（线上一段还没播完就问下一题，会让连接卡住并断线）。
* **麦克风帧改由独立任务编码。** Opus 编码器峰值栈远超音频任务的 4 KB，
  直接在音频任务里打开会在长按 UP 追问的第一帧触发栈保护重启
  （`Stack protection fault`，任务 `ecard_audio`）。现在音频任务只负责凑满
  60 ms 帧并交给会话期间创建的 8 KB `xz_enc` 任务。
* **堆余量决定成败。** TLS 1.3 + Opus 大约需要 40 KB 空闲堆；WebSocket 连接
  那一步只剩 8 KB 时，mbedTLS 直接失败
  （`mbedtls_ssl_handshake returned -0x2880`），所有 AI 入口都停在“连接中”。
  三处调整换出余量：LVGL 绘制缓冲 40 行→24 行（约 7.7 KB）、麦克风编码任务
  只在按住 UP 期间存在（11.8 KB）、录音上限改成 10 秒（40 KB）并在进入 AI
  会话前释放。
* **按住说话时用编码器换掉解码器。** 追问期间关闭解码器/解码任务，把栈与
  队列内存让给编码任务；`listen stop` 后释放编码任务，下一帧 TTS 再按需重开
  解码器。编码器必须和编码任务一起关闭；只关任务不关编码器会让解码器在
  “大块内存仍被占着”的情况下打开，持续返回 `-2`，TTS 就没有声音。
* **编码器的连续内存来自静态后备区。** `esp_audio_codec` 的
  `media_lib_module_malloc/calloc/realloc/free` 是弱符号，`main/ecard_opus_arena.c`
  用强符号覆盖它们：只在 `ecard_opus_arena_begin()`→`esp_audio_enc_open()`→
  `ecard_opus_arena_end()` 窗口内、请求 ≥12 KB 时从 25 KB 静态后备区分出一块，
  其余分配仍走系统堆。TLS 握手 + Wi-Fi 缓冲把系统堆打碎后最大连续块只有
  ~18 KB，靠 `malloc` 永远拿不到编码器需要的那一块（`OPUS_ALLOC_FAIL(-7)`）。
  真机实测 16 kHz / 60 ms / VOIP 编码器申请 **24524 字节**；后备区原来只声明
  20 KB 却仍返回这块内存，libopus 写穿数组、踩坏紧邻的 LVGL 内存池，设备随后
  在 `lv_tlsf` 崩溃重启。现在超过后备区容量的请求会直接失败，不会再写越界。
* **编码器不能在建 TLS 之前预打开。** 试过：编码器（24.5 KB）+ 解码器 + WS 客户端
  一起驻留时，连接前只剩 8 KB，`mbedtls_ssl_handshake returned -0x2880`；而且
  驻留的编码器会把解码器挤掉，古诗/单词的 TTS 变成“只有字幕没有声音”。
* **Wi-Fi 动态缓冲是最大的一笔可回收内存。** 设备只维持一条小智 WebSocket，
  默认 32+32 个动态 TX/RX 缓冲约 50 KB。`sdkconfig.defaults` 改成 8+8、
  静态 RX 8、管理帧 3，给 TLS/Opus 留出余量。
* **服务端按 60 ms 解码设备音频。** `xiaozhi-server` 的 `_decode_opus_packet`
  固定 `decoder.decode(packet, 960)`，所以编码器只能发 60 ms 帧（960 样本 @16 kHz），
  20/10 ms 帧会被解坏；不要为了省内存改帧长。
* **长文本被拒后必须先 `abort`。** 实测服务端发出
  `Detect is only for wake words...` 的 alert 后，同一会话里再发短 `detect`
  会被静默丢弃；设备收到该 alert 会先发 `abort` 复位，再发送备用短提示词。
* **录音缓冲只在录音时占内存。** 10 秒缓冲常驻要 40 KB，会把 TLS/Opus 挤到
  只剩约 10 KB：解码器直接 `ret -7`（所有 TTS 都不响），服务端 `hello` 也
  收不到。现在进入 AI 会话即释放这段缓冲，下次录音再按空闲堆分配，正常仍是
  10 秒。
* **Opus 解码器必须在建 TLS 之前打开。** 放到握手之后只剩少量连续堆，
  `ESP_OPUS_DEC` 打不开，TTS 完全没有声音。
* **断线自愈。** 若 WebSocket 已连上但一直收不到服务端 `hello`，发送任务会在
  5 秒后重发一次 `hello`，之后每 3 秒一次、最多 4 次，并逐次打日志；断开与
  服务端关闭都会清掉 `tts_active` 和 PCM 缓冲，界面不会再卡在“正在说话”。
* **追问有明确反馈。** 答题/古诗/单词页长按 UP 会弹出“小智正在听”浮层
  （“松开 UP 发送 · 说完再松手”“最长 10 秒”），持续上传 Opus 帧，松开即发
  `listen stop`；满 10 秒自动停止并发送。会话尚在连接时，页面底部会提示
  “小智连接中”，而不是静默忽略。进入任一 AI 页面即后台预连接 OTA/WebSocket。
* **菜单与未读状态。** 菜单项在按键按下沿立即移动（随后的 `CLICK` 被消费），
  按住稍久不再出现延迟；课间轮询不再伪造未读留言。
* **提示词有两条实测上限。** 逐条打官方服务端确认：含中文的
  `listen`/`detect` 文本 16 字符通过、18 字符被拒；纯英文 30 字节通过、
  36 字节被拒，超限都会返回
  `Detect is only for wake words, do not send long texts.` 并丢弃整轮对话。
  因此提示词改为在“卡片数据层”构造（`main/ecard_ai_prompt.c`）：
  - 答题：题库 `ai_prompt` 全部 ≤16 字，超长兜底为“讲题”；
  - 古诗：`上句|下句` ≤15 字，不退化；
  - 单词：分两轮发送。第一轮 `读:单词,词义`（≤16 字，`present,现在；礼物` 恰好
    16 字；更长词义退化成 `读:单词`，不切半截词义），第一轮播完设备自动追问
    第二轮——卡片上的原句例句（纯英文 ≤30 字节）。小智靠上一轮上下文知道
    讲的是哪个词，因此第二轮能朗读并翻译原句，而不是自己编例句。
  `tests/test_ecard_ai_prompt.c` 遍历全部 10 道题、60 对古诗、293 张单词卡
  逐条断言不超限。
* **提示词等服务端 session_id。** 只有收到服务端 `hello` 后才发 `detect`；
  提前发送会和握手撞车，问题被静默丢弃。
* **本机留言回放真实录音。** 试听结束不再释放录音缓冲，下次录音开始才覆盖；
  只有拥有该缓冲的那条留言（`s_clip_message_id`）用它回放，网络下发的留言
  仍使用内置演示语音（暂时没有音频负载）。
* 仍未完成：把转写/回答显示到 eCard 界面；网络留言在小智回声链路实机确认前
  仍使用内置演示语音。

## 四、各能力落到哪里

| 需求 | eCard 接入点 | 服务端消息 |
| Wi-Fi 配置 | 本地 SoftAP 配网页（已实现），后续可复用 `wifi_manager` | 无 |
| 语音留言转文字 | `ecard_voice.c` 录音，上传 Opus，播放时同步显示文字 | `listen` / `stt` / `tts` |
| 答题解析与多轮 | `ecard_quiz.c` 请求解析，后续追问保持上下文 | `llm` 文字流、`mcp` 工具 |
| 古诗/单词朗读 | 翻卡时按当前文字请求 TTS | `tts` Opus 帧 |
| 时间校准 | 小智协议不提供 → 已用 SNTP 实现 | 无 |

## 五、Flash 预算

| 项目 | 字节 |
| --- | ---: |
| 删除发音资源后的 eCard 应用 | ~4.0 MB |
| 中文点阵字库（9/12/16/22/48 px） | ~2.3 MB |
| Opus 编解码（`esp_opus`） | ~0.3 MB |
| WebSocket + JSON + NVS 配置 | ~0.2 MB |
| TLS（mbedTLS，已链接） | ~0.3 MB |
| 卡表与余量 | ~1.4 MB |

字库工作改变了这笔账：去掉 GB2312 兜底并改用 2 bpp 后，字库从 2.3 MB 降到 346 KB，
整个应用只有 1.96 MB。因此本分支已把 `partitions.csv` 换成双 OTA：

```text
nvs      0x9000   0x6000   （偏移与大小不变，配置不丢）
phy_init 0xf000   0x1000
otadata  0x10000  0x2000
ota_0    0x20000  0x3f0000  (4.13 MB)
ota_1    0x410000 0x3f0000  (4.13 MB)
```

运行镜像在 `ota_0`；OTA 升级写入 `ota_1` 并切换 `otadata`，升级失败可回滚。
从旧的单 factory 表切换仍需要一次 USB 烧录合并镜像，因为分区表本身变了。

## 六、待确认

1. **服务端地址与激活方式**：小智 WebSocket URL，以及设备如何绑定（激活码或预置 token）。
   参考分支通过 OTA/激活流程获取，本部署需要具体值。
2. **音频链路**：小智要求 Opus。当前录音/回放是 16-bit PCM，接入需要 Opus 编码上传与
   解码回放，并按服务端下发的采样率重采样。
3. **OTA 传输**：分区已具备双 OTA 能力，升级客户端仍待实现。
