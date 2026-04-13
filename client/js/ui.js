export function showScreen(id) {
    for (const el of document.querySelectorAll(".screen")) el.classList.add("hidden");
    document.getElementById(id).classList.remove("hidden");
}

export function renderLobby(state, me) {
    document.getElementById("lobby-badge").textContent = state.room_code;
    const countEl = document.getElementById("lobby-count");
    if (countEl) countEl.textContent = state.players.length;

    const ul = document.getElementById("lobby-players");
    ul.innerHTML = "";
    state.players.forEach((nick, i) => {
        const li = document.createElement("li");
        li.style.animationDelay = `${i * 60}ms`;
        if (nick === me) li.classList.add("is-me");

        const dot = document.createElement("span");
        dot.className = "seat-dot";
        li.appendChild(dot);

        const nameWrap = document.createElement("span");
        nameWrap.className = "player-name";
        nameWrap.textContent = nick;
        li.appendChild(nameWrap);

        if (nick === me) {
            const meTag = document.createElement("span");
            meTag.className = "me-tag-list";
            meTag.textContent = "나";
            li.appendChild(meTag);
        }

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

    const potEl = document.getElementById("game-pot");
    if (potEl.textContent !== String(state.pot)) {
        potEl.textContent = state.pot;
        potEl.style.transform = "scale(1.12)";
        setTimeout(() => { potEl.style.transform = ""; }, 240);
    }

    // Pot sub-text: current turn / phase hint
    const potTurn = document.getElementById("pot-turn");
    if (state.phase === "BETTING") {
        if (state.current_turn === me) {
            potTurn.innerHTML = `<strong>내 턴</strong> · 베팅 ${state.current_bet}`;
        } else {
            potTurn.innerHTML = `${state.current_turn} 차례 · 베팅 ${state.current_bet}`;
        }
    } else if (state.phase === "HAND_OVER") {
        potTurn.innerHTML = "핸드 종료";
    } else if (state.phase === "GAME_OVER") {
        potTurn.innerHTML = "게임 종료";
    } else {
        potTurn.innerHTML = "";
    }

    const container = document.getElementById("game-players");
    container.innerHTML = "";

    state.players.forEach((p) => {
        const div = document.createElement("div");
        div.className = "player-card animate-fade-in";
        if (p.nickname === me) div.classList.add("is-me");
        if (p.nickname === state.current_turn && state.phase === "BETTING") {
            div.classList.add("is-turn");
        }
        if (p.status === "folded") div.classList.add("is-folded");

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

        const meTag = p.nickname === me ? `<span class="me-tag">나</span>` : "";

        div.innerHTML = `
            ${turnBadge}
            ${statusHtml}
            <div class="name">${escapeHtml(p.nickname)}${meTag}</div>
            <div class="card-value ${hiddenClass}">${cardDisplay}</div>
            <div class="chips-row"><span class="chips-icon"></span>${p.chips}</div>
            <div class="bet-line">베팅 <b>${p.bet_this_round}</b></div>
        `;
        container.appendChild(div);
    });

    const actions = document.getElementById("game-actions");
    if (state.current_turn === me && state.phase === "BETTING") {
        actions.classList.remove("hidden");
        // Pre-fill raise amount with min-raise target
        const input = document.getElementById("raise-amount");
        if (!input.value) {
            input.value = state.current_bet + state.min_raise;
        }
    } else {
        actions.classList.add("hidden");
    }
}

function escapeHtml(s) {
    return String(s).replace(/[&<>"']/g, c => ({
        "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;"
    }[c]));
}

/* ── Toast system ──────────────────────────────────────── */
let toastStack = null;
function ensureStack() {
    if (!toastStack) toastStack = document.getElementById("toast-stack");
    return toastStack;
}

export function toast(text, variant = "info", duration = 3500) {
    const stack = ensureStack();
    if (!stack) return;
    const el = document.createElement("div");
    el.className = `toast ${variant}`;
    el.textContent = text;
    stack.appendChild(el);

    // Auto-dismiss
    setTimeout(() => {
        el.classList.add("leaving");
        setTimeout(() => el.remove(), 320);
    }, duration);
}

// Backwards-compat: setMessage now routes to toast
export function setMessage(text, variant = "info") {
    if (!text) return;
    toast(text, variant);
}
