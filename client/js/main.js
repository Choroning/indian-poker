import { WSClient } from "./ws_client.js";
import { showScreen, renderLobby, renderGame, setMessage } from "./ui.js";

const ws = new WSClient();
let myNick = null;
let roomCode = null;
let latestState = null;

async function ensureOpen() {
    if (!ws.ws || ws.ws.readyState > 1) await ws.open();
}

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
        ws.send({ type: "action", action: "raise", amount });
    } else {
        ws.send({ type: "action", action });
    }
});

ws.on("room_created", (msg) => {
    roomCode = msg.room_code;
    sessionStorage.setItem("ip_room_code", roomCode);
    showScreen("lobby");
    renderLobby({ room_code: roomCode, players: [myNick], host: myNick });
    document.getElementById("btn-start").hidden = false;
});

ws.on("joined_room", (msg) => {
    roomCode = msg.room_code;
    sessionStorage.setItem("ip_room_code", roomCode);
});

ws.on("lobby_update", (msg) => {
    showScreen("lobby");
    renderLobby(msg);
    document.getElementById("btn-start").hidden = (msg.host !== myNick);
});

ws.on("game_state", (msg) => {
    latestState = msg;
    showScreen("game");
    renderGame(msg, myNick);
});

ws.on("hand_over", (msg) => {
    setMessage(`Hand ${msg.hand_number}: ${msg.winner} won ${msg.pot_won}.`);
    setTimeout(() => ws.send({ type: "ready_next_hand" }), 2000);
});

ws.on("game_over", (msg) => {
    setMessage(`GAME OVER — ${msg.winner} wins!`);
});

ws.on("player_disconnected", (msg) => setMessage(`${msg.nickname} disconnected…`));
ws.on("player_reconnected", (msg) => setMessage(`${msg.nickname} reconnected.`));
ws.on("player_left",         (msg) => setMessage(`${msg.nickname} left (${msg.reason}).`));
ws.on("error",               (msg) => setMessage(`Error: ${msg.code} — ${msg.message}`));
