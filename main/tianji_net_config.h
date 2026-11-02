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
// 局域网直连极速接入点（首选：同在手机热点/局域网下，<5ms延迟，0 TLS内存开销，永无代理超时）
#ifndef TIANJI_GATEWAY_LAN_URL
#define TIANJI_GATEWAY_LAN_URL        "http://192.168.192.104:8765/api/v1/divination/voice"
#endif

#ifndef TIANJI_GATEWAY_LAN_AUDIO_BASE
#define TIANJI_GATEWAY_LAN_AUDIO_BASE "http://192.168.192.104:8765"
#endif

// 公网 Tailscale 域名备份接入点（备用：外出不在同一局域网时自动回退）
#ifndef TIANJI_GATEWAY_DOMAIN_URL
#define TIANJI_GATEWAY_DOMAIN_URL     "https://macminim4.tail20709.ts.net/api/v1/divination/voice"
#endif

#ifndef TIANJI_GATEWAY_DOMAIN_AUDIO_BASE
#define TIANJI_GATEWAY_DOMAIN_AUDIO_BASE "https://macminim4.tail20709.ts.net"
#endif

// 默认首选接入点（按要求默认为公网 Tailscale 域名，支持外出随身随地推演）
#ifndef TIANJI_GATEWAY_URL
#define TIANJI_GATEWAY_URL            TIANJI_GATEWAY_DOMAIN_URL
#endif

#ifndef TIANJI_GATEWAY_AUDIO_BASE
#define TIANJI_GATEWAY_AUDIO_BASE     TIANJI_GATEWAY_DOMAIN_AUDIO_BASE
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
