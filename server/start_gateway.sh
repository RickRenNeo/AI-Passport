#!/usr/bin/env bash
# 启动《天机·六爻》语音网关服务

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$DIR"

PORT="${PORT:-8765}"
LOG_FILE="$DIR/server/gateway.log"
PID_FILE="$DIR/server/gateway.pid"

if [ -f "$PID_FILE" ] && kill -0 "$(cat "$PID_FILE")" 2>/dev/null; then
    echo "网关服务已在运行 (PID: $(cat "$PID_FILE"))"
    exit 0
fi

echo "正在启动网关服务 (端口: $PORT)..."
nohup "$DIR/venv_gateway/bin/python" -m uvicorn server.tianji_gateway:app --host 0.0.0.0 --port "$PORT" > "$LOG_FILE" 2>&1 &
echo $! > "$PID_FILE"

sleep 2
if kill -0 "$(cat "$PID_FILE")" 2>/dev/null; then
    echo "网关服务启动成功！"
    echo "  - PID: $(cat "$PID_FILE")"
    echo "  - 端口: $PORT"
    echo "  - 日志文件: $LOG_FILE"
else
    echo "网关服务启动失败，请检查日志: $LOG_FILE"
    exit 1
fi
