export function showScreen(id) {
    for (const el of document.querySelectorAll(".screen")) el.classList.add("hidden");
    document.getElementById(id).classList.remove("hidden");
}

export function renderLobby(state) {
    document.getElementById("lobby-code").textContent = state.room_code;
    const ul = document.getElementById("lobby-players");
    ul.innerHTML = "";
    for (const nick of state.players) {
        const li = document.createElement("li");
        li.textContent = nick + (nick === state.host ? " (host)" : "");
        ul.appendChild(li);
    }
}

export function renderGame(state, me) {
    document.getElementById("game-code").textContent = state.room_code;
    document.getElementById("game-hand").textContent = state.hand_number;
    document.getElementById("game-pot").textContent = state.pot;
    const container = document.getElementById("game-players");
    container.innerHTML = "";
    for (const p of state.players) {
        const div = document.createElement("div");
        div.className = "player-card";
        if (p.nickname === me) div.classList.add("me");
        if (p.nickname === state.current_turn) div.classList.add("turn");
        const card = p.card === null ? "?" : p.card;
        div.innerHTML = `
            <div><strong>${p.nickname}</strong></div>
            <div class="card ${p.card === null ? "hidden-card" : ""}">${card}</div>
            <div class="chips">${p.chips} chips · bet ${p.bet_this_round}</div>
            <div class="status">${p.status}</div>
        `;
        container.appendChild(div);
    }
    const actions = document.getElementById("game-actions");
    if (state.current_turn === me && state.phase === "BETTING") {
        actions.classList.remove("hidden");
    } else {
        actions.classList.add("hidden");
    }
}

export function setMessage(text) {
    document.getElementById("game-message").textContent = text || "";
}
