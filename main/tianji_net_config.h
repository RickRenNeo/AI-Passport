// main/tianji_net_config.h — 《天机·六爻》网络与 oMLX (MiniCPM 5) 连接配置
#pragma once

// ============================================================================
// 1. 手机 Wi-Fi 热点配置
// ============================================================================
// 请在此处修改为您手机热点的实际名称与密码：
#ifndef TIANJI_WIFI_SSID
#define TIANJI_WIFI_SSID        "NEO"
#endif

#ifndef TIANJI_WIFI_PASS
#define TIANJI_WIFI_PASS        "ren830804"
#endif

// ============================================================================
// 2. oMLX (MiniCPM 5) 服务端配置
// ============================================================================
// Mac 电脑通过 Tailscale Funnel 提供公共安全端点服务
#define TIANJI_OMLX_HOST        "macminim4.tail20709.ts.net"
#define TIANJI_OMLX_PORT        443
#define TIANJI_OMLX_URL         "https://macminim4.tail20709.ts.net/v1/chat/completions"

// 9999 端口启用了强制 Bearer Token 认证，填入真实 oMLX API Key
#ifndef TIANJI_OMLX_API_KEY
#define TIANJI_OMLX_API_KEY     "sk-omlx-local"
#endif

// 模型名称（必须与 oMLX /v1/models 列表中的 ID 严格匹配）
#ifndef TIANJI_OMLX_MODEL
#define TIANJI_OMLX_MODEL       "MiniCPM5-2B-MLX"
#endif

// HTTP 超时时间（毫秒，本地大模型推理需要适当留出时间）
#define TIANJI_OMLX_TIMEOUT_MS  45000
