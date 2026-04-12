import { WSClient } from "./ws_client.js";
import { showScreen, renderLobby, renderGame, setMessage } from "./ui.js";

const ws = new WSClient();
let myNick = null;
let roomCode = null;

async function ensureOpen() {
    if (!ws.ws || ws.ws.readyState > 1) await ws.open();
}

/* ── Landing: quiz-app style expand/collapse ──────────── */
const slotCreate = document.getElementById("slot-create");
const slotJoin = document.getElementById("slot-join");
const choiceRow = document.querySelector(".choice-row");
let expandedSlot = null;

function collapseAll() {
    slotCreate.classList.remove("is-expanded", "is-hidden");
    slotJoin.classList.remove("is-expanded", "is-hidden");
    choiceRow.classList.remove("has-expanded");
    for (const btn of document.querySelectorAll(".choice-close")) btn.remove();
    expandedSlot = null;
}

function expandSlot(slot) {
    const other = slot === slotCreate ? slotJoin : slotCreate;
    slot.classList.add("is-expanded");
    slot.classList.remove("is-hidden");
    other.classList.add("is-hidden");
    other.classList.remove("is-expanded");
    choiceRow.classList.add("has-expanded");

    const label = slot.querySelector(".choice-label");
    if (!slot.querySelector(".choice-close")) {
        const close = document.createElement("button");
        close.className = "choice-close";
        close.textContent = "\u2715";
        close.setAttribute("aria-label", "닫기");
        close.addEventListener("click", (e) => { e.stopPropagation(); collapseAll(); });
        label.after(close);
    }
    expandedSlot = slot;
}

for (const label of document.querySelectorAll(".choice-label")) {
    label.addEventListener("click", () => {
        const slot = label.closest(".choice-slot");
        if (slot.classList.contains("is-expanded")) {
            collapseAll();
        } else {
            expandSlot(slot);
        }
    });
}

/* ── Form submissions ─────────────────────────────────── */
document.getElementById("form-create").addEventListener("submit", async (e) => {
    e.preventDefault();
    const data = new FormData(e.target);
    myNick = data.get("nickname");
    await ensureOpen();
    ws.send({
        type: "create_room",
        nickname: myNick,
        max_players: parseInt(data.get("max_players"), 10),
    });
});

document.getElementById("form-join").addEventListener("submit", async (e) => {
    e.preventDefault();
    const data = new FormData(e.target);
    myNick = data.get("nickname");
    roomCode = data.get("room_code");
    await ensureOpen();
    ws.send({ type: "join_room", room_code: roomCode, nickname: myNick });
});

document.getElementById("btn-start").addEventListener("click", () => {
    ws.send({ type: "start_game" });
});

document.getElementById("game-actions").addEventListener("click", (e) => {
    const btn = e.target.closest("button[data-action]");
    if (!btn) return;
    const action = btn.dataset.action;
    if (action === "raise") {
        const amount = parseInt(document.getElementById("raise-amount").value, 10);
        if (isNaN(amount) || amount <= 0) return;
        ws.send({ type: "action", action: "raise", amount });
    } else {
        ws.send({ type: "action", action });
    }
});

/* ── WebSocket handlers ───────────────────────────────── */
ws.on("room_created", (msg) => {
    roomCode = msg.room_code;
    showScreen("lobby");
    renderLobby({ room_code: roomCode, players: [myNick], host: myNick });
    document.getElementById("btn-start").hidden = false;
});

ws.on("joined_room", (msg) => {
    roomCode = msg.room_code;
});

ws.on("lobby_update", (msg) => {
    showScreen("lobby");
    renderLobby(msg);
    document.getElementById("btn-start").hidden = (msg.host !== myNick);
});

ws.on("game_state", (msg) => {
    showScreen("game");
    renderGame(msg, myNick);
});

ws.on("hand_over", (msg) => {
    setMessage(`핸드 ${msg.hand_number}: ${msg.winner} 승리 (+${msg.pot_won} 칩)`);
    setTimeout(() => ws.send({ type: "ready_next_hand" }), 2000);
});

ws.on("game_over", (msg) => {
    setMessage(`게임 종료 — ${msg.winner} 최종 승리!`);
});

ws.on("player_disconnected", (msg) => setMessage(`${msg.nickname} 연결 끊김...`));
ws.on("player_reconnected", (msg) => setMessage(`${msg.nickname} 재접속`));
ws.on("player_left",         (msg) => setMessage(`${msg.nickname} 퇴장 (${msg.reason === "timeout" ? "시간 초과" : msg.reason})`));
ws.on("error", (msg) => {
    const codes = {
        ROOM_NOT_FOUND: "방을 찾을 수 없습니다",
        NICKNAME_TAKEN: "이미 사용 중인 닉네임입니다",
        NICKNAME_INVALID: "올바르지 않은 닉네임입니다",
        ROOM_FULL: "방이 가득 찼습니다",
        ROOM_IN_PROGRESS: "이미 게임이 진행 중입니다",
        NOT_HOST: "호스트만 시작할 수 있습니다",
        NOT_ENOUGH_PLAYERS: "2명 이상 필요합니다",
        RATE_LIMITED: "요청이 너무 많습니다",
        MALFORMED_MESSAGE: "잘못된 요청입니다",
    };
    setMessage(codes[msg.code] || `오류: ${msg.message}`);
});
