export function showScreen(id) {
    for (const el of document.querySelectorAll(".screen")) el.classList.add("hidden");
    document.getElementById(id).classList.remove("hidden");
}

export function renderLobby(state) {
    document.getElementById("lobby-badge").textContent = state.room_code;
    const ul = document.getElementById("lobby-players");
    ul.innerHTML = "";
    state.players.forEach((nick, i) => {
        const li = document.createElement("li");
        li.style.animationDelay = `${i * 60}ms`;

        const dot = document.createElement("span");
        dot.className = "seat-dot";
        li.appendChild(dot);

        const name = document.createTextNode(nick);
        li.appendChild(name);

        if (nick === state.host) {
            const tag = document.createElement("span");
            tag.className = "host-tag";
            tag.textContent = "HOST";
            li.appendChild(tag);
        }
        ul.appendChild(li);
    });
}

export function renderGame(state, me) {
    document.getElementById("game-hand").textContent = state.hand_number;
    document.getElementById("game-pot").textContent = state.pot;

    const container = document.getElementById("game-players");
    container.innerHTML = "";

    state.players.forEach((p) => {
        const div = document.createElement("div");
        div.className = "player-card animate-fade-in";
        if (p.nickname === me) div.classList.add("is-me");
        if (p.nickname === state.current_turn && state.phase === "BETTING") {
            div.classList.add("is-turn");
        }

        const cardDisplay = p.card === null ? "?" : p.card;
        const hiddenClass = p.card === null ? "is-hidden" : "";

        let statusHtml = "";
        if (p.status === "folded") {
            statusHtml = `<span class="status-tag folded">FOLD</span>`;
        } else if (p.status === "eliminated") {
            statusHtml = `<span class="status-tag eliminated">OUT</span>`;
        }

        let turnBadge = "";
        if (p.nickname === state.current_turn && state.phase === "BETTING") {
            turnBadge = `<span class="turn-badge">TURN</span>`;
        }

        const label = p.nickname === me ? `${p.nickname} (나)` : p.nickname;

        div.innerHTML = `
            ${turnBadge}
            <div class="name">${label}</div>
            <div class="card-value ${hiddenClass}">${cardDisplay}</div>
            <div class="info"><strong>${p.chips}</strong> 칩 · 베팅 <strong>${p.bet_this_round}</strong></div>
            ${statusHtml}
        `;
        container.appendChild(div);
    });

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
