#!/usr/bin/env bash
# 停止《天机·六爻》语音网关服务

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PID_FILE="$DIR/server/gateway.pid"

if [ -f "$PID_FILE" ]; then
    PID="$(cat "$PID_FILE")"
    if kill -0 "$PID" 2>/dev/null; then
        echo "正在停止网关服务 (PID: $PID)..."
        kill "$PID"
        sleep 1
        kill -9 "$PID" 2>/dev/null || true
    fi
    rm -f "$PID_FILE"
    echo "网关服务已停止。"
else
    # 兜底清理可能残留的进程
    pkill -f "uvicorn server.tianji_gateway:app" 2>/dev/null || true
    echo "未发现运行中的网关 PID，已清理残留进程。"
fi
