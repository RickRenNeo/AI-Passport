// main/tianji_net_config.h — 《天机·六爻》网络与网关连接配置
#pragma once

// ============================================================================
// 1. 手机 Wi-Fi 热点配置
// ============================================================================
#ifndef TIANJI_WIFI_SSID
#define TIANJI_WIFI_SSID        "NEO"
#endif

#ifndef TIANJI_WIFI_PASS
#define TIANJI_WIFI_PASS        "ren830804"
#endif

// ============================================================================
// 2. 语音解卦网关配置 (SenseVoice ASR + MiniCPM 5 LLM + EdgeTTS)
// ============================================================================
// 公网 Tailscale 域名接入点（已通过 Funnel 映射 /api 路径到端口 8765）
#ifndef TIANJI_GATEWAY_URL
#define TIANJI_GATEWAY_URL        "https://macminim4.tail20709.ts.net/api/v1/divination/voice"
#endif

#ifndef TIANJI_GATEWAY_AUDIO_BASE
#define TIANJI_GATEWAY_AUDIO_BASE "https://macminim4.tail20709.ts.net"
#endif

// 局域网直连备份接入点（同一 Wi-Fi 热点下直连，无 TLS 握手内存开销）
#ifndef TIANJI_GATEWAY_LAN_URL
#define TIANJI_GATEWAY_LAN_URL    "http://192.168.192.104:8765/api/v1/divination/voice"
#endif

#ifndef TIANJI_GATEWAY_LAN_AUDIO_BASE
#define TIANJI_GATEWAY_LAN_AUDIO_BASE "http://192.168.192.104:8765"
#endif

// HTTP 超时时间（毫秒）
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
