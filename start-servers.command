#!/bin/bash
# Indian Poker — macOS launcher
# Double-click to start both the WebSocket server and the static client server.

set -e
cd "$(dirname "$0")"

SERVER_BIN="./server/build/ip_server"
CLIENT_DIR="./client"
WS_PORT=8080
STATIC_PORT=3000

# ── Preflight checks ─────────────────────────────────────
if [ ! -x "$SERVER_BIN" ]; then
    echo "❌ WebSocket server binary not found at $SERVER_BIN"
    echo "   Build it first:"
    echo "     cmake -S server -B server/build && cmake --build server/build -j"
    echo ""
    read -n 1 -s -r -p "Press any key to close..."
    exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
    echo "❌ python3 not found (needed for static file server)"
    read -n 1 -s -r -p "Press any key to close..."
    exit 1
fi

# ── Stop anything already on those ports ────────────────
kill $(lsof -ti:$WS_PORT) 2>/dev/null || true
kill $(lsof -ti:$STATIC_PORT) 2>/dev/null || true
sleep 0.3

# ── Start servers ────────────────────────────────────────
echo "▶ Starting WebSocket server on :$WS_PORT"
"$SERVER_BIN" &
WS_PID=$!

echo "▶ Starting static file server on :$STATIC_PORT"
python3 -m http.server $STATIC_PORT --directory "$CLIENT_DIR" >/dev/null 2>&1 &
STATIC_PID=$!

sleep 0.5

echo ""
echo "══════════════════════════════════════════"
echo "  Indian Poker 서버 실행 중"
echo "  → http://localhost:$STATIC_PORT"
echo "══════════════════════════════════════════"
echo ""
echo "브라우저를 자동으로 엽니다…"
open "http://localhost:$STATIC_PORT"

echo ""
echo "이 창을 닫거나 Ctrl+C를 누르면 서버가 종료됩니다."

# ── Graceful shutdown ────────────────────────────────────
cleanup() {
    echo ""
    echo "⏹  Stopping servers..."
    kill $WS_PID $STATIC_PID 2>/dev/null || true
    exit 0
}
trap cleanup INT TERM

wait
