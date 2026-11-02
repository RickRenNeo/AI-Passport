// main/tianji_net_config.h — 《天机·六爻》网络与网关连接配置
#pragma once

// ============================================================================
// 1. 手机 Wi-Fi 热点配置
// ============================================================================
// 请在此修改为手机热点的实际名称与密码：
#ifndef TIANJI_WIFI_SSID
#define TIANJI_WIFI_SSID        "NEO"
#endif

#ifndef TIANJI_WIFI_PASS
#define TIANJI_WIFI_PASS        "ren830804"
#endif

// ============================================================================
// 2. 语音解卦网关配置 (SenseVoice ASR + MiniCPM 5 LLM + EdgeTTS)
// ============================================================================
// 网关服务器可通过公网 Tailscale 域名或局域网 IP 直连
#ifndef TIANJI_GATEWAY_HOST
#define TIANJI_GATEWAY_HOST       "macminim4.tail20709.ts.net"
#endif

#ifndef TIANJI_GATEWAY_PORT
#define TIANJI_GATEWAY_PORT       443
#endif

#ifndef TIANJI_GATEWAY_URL
#define TIANJI_GATEWAY_URL        "https://macminim4.tail20709.ts.net/api/v1/divination/voice"
#endif

#ifndef TIANJI_GATEWAY_AUDIO_BASE
#define TIANJI_GATEWAY_AUDIO_BASE "https://macminim4.tail20709.ts.net"
#endif

// HTTP 超时时间（毫秒，包含 ASR + LLM + TTS 全链路）
#define TIANJI_GATEWAY_TIMEOUT_MS 60000

// ============================================================================
// 3. 原生 oMLX (MiniCPM 5) 服务端备用配置
// ============================================================================
#define TIANJI_OMLX_HOST          "macminim4.tail20709.ts.net"
#define TIANJI_OMLX_PORT          443
#define TIANJI_OMLX_URL           "https://macminim4.tail20709.ts.net/v1/chat/completions"

#ifndef TIANJI_OMLX_API_KEY
#define TIANJI_OMLX_API_KEY       "sk-omlx-local"
#endif

#ifndef TIANJI_OMLX_MODEL
#define TIANJI_OMLX_MODEL         "MiniCPM5-2B-MLX"
#endif

#define TIANJI_OMLX_TIMEOUT_MS    45000
