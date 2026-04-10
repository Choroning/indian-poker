# Indian Poker — Design Document

**Status:** Design finalized, implementation not yet started.
**Last updated:** 2026-04-11
**Author:** Cheolwon Park
**Assisted by:** Claude (Anthropic) — used as a brainstorming and design review partner.

---

## Table of Contents

1. [Overview and Goals](#1-overview-and-goals)
2. [Architecture](#2-architecture)
3. [Technology Choices](#3-technology-choices)
4. [Application Protocol](#4-application-protocol)
5. [Indian Poker Game Module](#5-indian-poker-game-module)
6. [Room Lifecycle, Disconnect, and Reconnect](#6-room-lifecycle-disconnect-and-reconnect)
7. [Security and Trust Model](#7-security-and-trust-model)
8. [Error Handling](#8-error-handling)
9. [Testing Strategy](#9-testing-strategy)
10. [Deployment Architecture](#10-deployment-architecture)
11. [Planned Directory Layout](#11-planned-directory-layout)
12. [Implementation Milestones](#12-implementation-milestones)
13. [Open Questions and Future Work](#13-open-questions-and-future-work)

---

## 1. Overview and Goals

### 1.1 What this project is

A real-time, browser-based multiplayer Indian Poker game, where 2–4 players join a room using a 6-character code and play several hands against each other. The server and WebSocket protocol are implemented from scratch in C++ for educational value; the client is intentionally kept minimal (vanilla JS).

### 1.2 Learning goals (the real purpose)

This is not primarily a "ship a polished product" project. It is a learning project whose goals, in priority order, are:

1. **Build real application-layer protocols on top of raw TCP.** Understand what it means to design a message format, frame boundaries, control flow, and state synchronization — not as an abstract lecture, but as concrete bytes on a wire.
2. **Implement the WebSocket protocol (RFC 6455) by hand.** Including the HTTP Upgrade handshake, frame parsing with client-to-server masking, fragmentation, and the control frames (ping/pong/close).
3. **Use `select(2)` for I/O multiplexing.** A single-threaded event loop that handles many clients without threads — reinforcing the concurrency model currently being taught in Operating Systems.
4. **Separate networking from game logic cleanly enough to plug in additional games later.** The game logic must be unit-testable with no sockets.
5. **Ship a working thing that friends can actually play together.** Over the Cloudflare Tunnel + Vercel pipeline — not deployed behind ten layers of framework magic, but understood end to end.

### 1.3 Explicit non-goals

To keep scope tight and protect the learning goals from scope creep:

- **Not** a serious poker game. No sidepots, no multi-round betting, no blinds structure.
- **Not** a multi-game platform in the MVP. Only Indian Poker is implemented; the architecture merely leaves room for more games.
- **Not** a full RFC 6455 implementation. Binary frames, extensions, and permessage-deflate are explicitly excluded.
- **Not** TLS in C++. The C++ server only speaks plain WebSocket; TLS is terminated by Cloudflare at the edge.
- **Not** a persistent-account system. No login, no user database, no match history. State lives in server memory only.
- **Not** a polished frontend. Vanilla JS, minimal HTML/CSS. No frontend framework, no build pipeline.
- **Not** production-grade reliability. No clustering, no graceful shutdown migration, no multi-server support.

---

## 2. Architecture

### 2.1 Three-layer server

The server is divided into three strictly layered modules. Each layer depends only on the layers below it. This separation is the single most important structural decision in the project.

```
+------------------------------------------------------------+
|  Layer 3: Game Logic                                       |
|    - IGame interface                                       |
|        start(players)                                      |
|        handle_action(player_id, action) -> Outgoing        |
|        snapshot_for(player_id) -> StateView                |
|        on_player_disconnect(player_id) -> Outgoing         |
|    - IndianPoker : IGame                                   |
+------------------------------------------------------------+
|  Layer 2: Session / Room Infrastructure                    |
|    - Player (nickname, chips, connection status)           |
|    - Room (code, player list, host, IGame instance)        |
|    - RoomRegistry (create/lookup/cleanup)                  |
|    - Router (decide which Connection(s) receive a message) |
+------------------------------------------------------------+
|  Layer 1: Network / Protocol                               |
|    - EventLoop (select fd_set, tick-based)                 |
|    - TCPListener                                           |
|    - Connection (one TCP socket, WS framing state)         |
|    - WebSocketHandshake                                    |
|    - WebSocketFrame (decode/encode, masking, fragments)    |
+------------------------------------------------------------+
```

### 2.2 Isolation rules

These rules are load-bearing. Violating them breaks the learning goal of clean separation:

- **Layer 1 never knows what a "room" or "player" is.** It delivers a fully-assembled text message (one complete WebSocket message, possibly reassembled from fragments) to an upper-layer callback, and it accepts an outgoing message to send. That's its entire contract.
- **Layer 2 never parses game messages or game rules.** It sees a message with a `type` field, routes it to the room's game instance if appropriate, and broadcasts the game's outgoing messages to the correct set of players.
- **Layer 3 never calls `send()`, never touches a file descriptor, never knows about WebSocket.** Given a game state and an action, it returns a new state and a list of `Outgoing` records, where each record specifies `{target: "broadcast" | player_id, payload: JSON}`. Layer 2 is responsible for turning that into network sends.

This last point is what makes game logic testable in isolation: a unit test can construct a state, call `handle_action`, and assert on the returned state + outgoing messages, without touching a socket.

### 2.3 State synchronization model

Server-to-client state sync uses **full-state broadcast with per-player redaction**, not event sourcing.

Rationale:

- After any state-changing action, the server computes the new full game state, then constructs a per-player view (a `snapshot_for(player_id)`) that redacts hidden information (e.g., that player's own card is nulled out). It sends this view to each connected player as a single `game_state` message.
- The client never computes game state locally. It receives a `game_state` snapshot and renders it. "The server is the single source of truth" is enforced structurally because the client has no logic to diverge.
- This makes reconnection trivial: on reconnect, the server sends a fresh `game_state` snapshot, and the client immediately has the correct view. No event replay, no state reconstruction.
- Messages are small (a few hundred bytes at most) because the Indian Poker state is tiny. Bandwidth is not a concern.

### 2.4 Client architecture

The client is deliberately minimal:

- Static HTML + CSS + vanilla JS. No React, Vue, Svelte, build step, or npm dependencies beyond what Vercel needs to serve static files.
- One WebSocket connection per session. Opened on first action (not on page load), reused until the page unloads.
- State is whatever the last `game_state` message said. UI renders from that. When a new `game_state` arrives, render is re-run (full DOM update).
- `sessionStorage` is used only to remember the current room code across accidental page refreshes, so the user doesn't lose their seat if they hit F5.

---

## 3. Technology Choices

### 3.1 Server

| Concern | Choice | Rationale |
|:--------|:-------|:----------|
| Language | C++ (C++17 or later) | Matches user's experience (C++ Practice course), exposes raw socket API naturally, no GC to think about |
| Build system | CMake | Standard for modern C++, integrates with clangd/VS Code easily |
| Sockets | POSIX BSD (`<sys/socket.h>`, `<netinet/in.h>`) | Same API on macOS (dev) and Linux (deploy), no abstraction layer |
| Concurrency | Single-threaded `select(2)` event loop | Current OS course topic; eliminates locks entirely; sufficient for tens of connections |
| WebSocket | RFC 6455 implemented by hand | **The point of the project.** Deliberately no library |
| Cryptography | OpenSSL (SHA-1 + base64 only) | Implementing SHA-1 is off-topic (crypto, not networking); wrapping it is 5 lines |
| JSON | [nlohmann/json](https://github.com/nlohmann/json) single-header | Picking a JSON lib is not the learning point; nlohmann is header-only and ergonomic |

### 3.2 Client

| Concern | Choice | Rationale |
|:--------|:-------|:----------|
| Language | Vanilla JavaScript (ES2022) | No framework tax, forces us to think about DOM and state directly |
| WebSocket | Browser-native `WebSocket` API | The client side of the handshake is already done for us by the browser |
| Markup/Style | HTML + hand-written CSS | The project is about networking, not frontend |
| State persistence | `sessionStorage` | Survives refresh, dies with tab — matches the 15s-ish reconnect intent |

### 3.3 Deployment

| Component | Service | Rationale |
|:----------|:--------|:----------|
| Static frontend | Vercel | Fast CDN, git push to deploy, free |
| TLS termination + public exposure | Cloudflare Tunnel (`cloudflared`) | User already has a Cloudflare domain; tunnel is free; lets us run the server on a laptop without any port forwarding |
| Backend runtime | Local machine (macOS dev, Linux friendly) | Binary, no container runtime required for MVP |

### 3.4 Things explicitly rejected

- **Socket.IO / any WebSocket library.** The point is to implement the protocol.
- **`epoll`/`kqueue`.** Adds platform-specific branches; `select` is the topic of the current OS course unit, and the scale does not need more.
- **Thread-per-connection.** Would miss the OS course connection; not needed at this scale.
- **React or any SPA framework.** Irrelevant to learning goals; adds a build step and hides DOM work.
- **Docker for local dev.** `cmake && make` is faster and simpler for a solo project.
- **Implementing TLS in C++.** TLS is a full-semester project on its own. Cloudflare terminates TLS.
- **Databases / Redis / any persistent store.** MVP has no persistence requirement. A server restart ends all rooms; acceptable.

---

## 4. Application Protocol

### 4.1 Transport

All application messages are **JSON-encoded UTF-8 text, carried in WebSocket text frames** (opcode `0x1`). One JSON object = one logical message. Messages may be fragmented at the WebSocket layer, but the JSON object is reassembled by Layer 1 before Layer 2 sees it.

Every message has a top-level `type` field. Unknown types are rejected with `MALFORMED_MESSAGE`.

### 4.2 Client → Server messages

```json
// Create a new room. The sender becomes the host.
{"type": "create_room", "nickname": "alice", "max_players": 4}

// Join an existing room. Used for both new joins and reconnects;
// the server decides which based on current room state.
{"type": "join_room", "room_code": "AB7K92", "nickname": "alice"}

// Host only, LOBBY phase only, requires at least 2 players.
{"type": "start_game"}

// A game action. Valid only during BETTING phase, on the sender's turn.
{"type": "action", "action": "check"}
{"type": "action", "action": "raise", "amount": 20}
{"type": "action", "action": "fold"}

// Acknowledge the end-of-hand screen, signalling readiness for the next hand.
{"type": "ready_next_hand"}

// Voluntarily leave the room.
{"type": "leave_room"}
```

### 4.3 Server → Client messages

```json
// Direct response to create_room.
{
  "type": "room_created",
  "room_code": "AB7K92",
  "you": "alice",
  "host": "alice"
}

// Direct response to join_room.
// "reconnect" is true if this was a reconnection into a slot held for "alice".
{
  "type": "joined_room",
  "room_code": "AB7K92",
  "you": "alice",
  "reconnect": false,
  "state": { /* same shape as game_state below */ }
}

// Broadcast when the lobby's player list or host changes.
{
  "type": "lobby_update",
  "room_code": "AB7K92",
  "players": ["alice", "bob"],
  "host": "alice",
  "max_players": 4
}

// THE main message. Sent after every state-changing event, to every connected
// player in the room. Each player receives their own view (card redacted).
{
  "type": "game_state",
  "room_code": "AB7K92",
  "phase": "BETTING",
  "hand_number": 3,
  "players": [
    {"nickname": "alice", "chips": 90, "card": null,  "bet_this_round": 10, "status": "active", "connected": true},
    {"nickname": "bob",   "chips": 80, "card": 7,     "bet_this_round": 20, "status": "active", "connected": true},
    {"nickname": "carol", "chips": 60, "card": null,  "bet_this_round": 0,  "status": "folded", "connected": true},
    {"nickname": "dave",  "chips": 40, "card": 2,     "bet_this_round": 20, "status": "active", "connected": false}
  ],
  "pot": 50,
  "current_turn": "alice",
  "current_bet": 20,
  "min_raise": 5,
  "you": "alice"
}
```

Card visibility rules in `game_state`:

- For each `player` entry, `card` is the card that **the receiving player** is allowed to see.
- Your own card is always `null` except during `SHOWDOWN` and `HAND_OVER` phases.
- A folded player's card is `null` for everyone until `SHOWDOWN` / `HAND_OVER`.
- A player not yet dealt (e.g., during `LOBBY`) has `card` = `null`.

Player `status` values: `active` (still in the hand), `folded` (folded this hand), `eliminated` (out of chips, out of the game), `disconnected_grace` (within 15s reconnect window — game is paused for them).

```json
// Broadcast at the end of each hand. Cards are fully revealed here.
{
  "type": "hand_over",
  "hand_number": 3,
  "winner": "bob",
  "pot_won": 50,
  "revealed": {"alice": 5, "bob": 9, "dave": 2},
  "eliminated_after_this_hand": [],
  "next_hand_starts_at_ms": 5000
}

// Broadcast when the game session ends (only one player has chips).
{
  "type": "game_over",
  "winner": "bob"
}

// Broadcast when a player's TCP connection drops.
// The hand is paused immediately; a timer starts for the grace period.
{
  "type": "player_disconnected",
  "nickname": "carol",
  "grace_period_ms": 15000
}

// Broadcast when a player successfully rejoins within the grace period.
{
  "type": "player_reconnected",
  "nickname": "carol"
}

// Broadcast when a player is removed permanently.
// "reason" is one of: "timeout", "voluntary".
{
  "type": "player_left",
  "nickname": "carol",
  "reason": "timeout"
}

// Direct response to any client message that failed.
// Never broadcast.
{
  "type": "error",
  "code": "NOT_YOUR_TURN",
  "message": "It's bob's turn, not yours."
}
```

### 4.4 Error codes

| Code | Meaning |
|:-----|:--------|
| `ROOM_NOT_FOUND` | `join_room` with a code that does not exist |
| `ROOM_FULL` | `join_room` when the room is at `max_players` |
| `ROOM_IN_PROGRESS` | `join_room` as a new player (not matching any disconnected slot) after the game started |
| `NICKNAME_TAKEN` | Nickname is currently held by a connected player in the room |
| `NICKNAME_INVALID` | Nickname fails validation (length, characters) |
| `NOT_HOST` | `start_game` sent by a non-host |
| `NOT_ENOUGH_PLAYERS` | `start_game` with fewer than 2 players |
| `NOT_YOUR_TURN` | `action` sent when it is not the sender's turn |
| `INVALID_ACTION` | `action` that does not make sense in the current phase |
| `INVALID_RAISE_AMOUNT` | Raise amount below `current_bet + min_raise`, or above chips available |
| `MALFORMED_MESSAGE` | JSON parse error or missing required field |
| `RATE_LIMITED` | Too many attempts from the same IP in a short window |
| `ORIGIN_NOT_ALLOWED` | Rejected at WebSocket handshake due to `Origin` header |
| `INTERNAL_ERROR` | Unexpected server error |

### 4.5 Typical message flows

**Flow A — Create a room and start a game:**

```
alice                                     server
  │                                          │
  ├──── WS handshake (Origin check) ────────▶│
  │◀────── 101 Switching Protocols ──────────┤
  │                                          │
  ├── {"type":"create_room","nickname":...}─▶│
  │                           (creates room, registers alice as host)
  │◀──────── room_created ───────────────────┤
  │◀──────── lobby_update ───────────────────┤  (1 player, alice)
  │                                          │
  │   (bob joins from another browser, goes through same flow)
  │◀──────── lobby_update ───────────────────┤  (2 players, alice+bob)
  │                                          │
  ├── {"type":"start_game"}─────────────────▶│
  │                           (LOBBY → DEALING → BETTING)
  │◀──────── game_state ─────────────────────┤  (BETTING, alice's turn)
```

**Flow B — Disconnect and reconnect mid-game:**

```
carol                                      server
  │                                           │
  │  (carol was in a hand, BETTING phase)     │
  │  ╳────── TCP connection dropped ─────────▶│
  │                            (server detects close, starts 15s timer)
  │                            (broadcasts to others in room:)
  │                               player_disconnected
  │                               game_state (carol.connected=false,
  │                                            phase unchanged, turn unchanged)
  │                                           │
  │  (8 seconds later, carol opens a new tab, clicks Rejoin)
  │                                           │
  ├──── WS handshake ──────────────────────▶│
  ├── {"type":"join_room","room_code":"AB7K92","nickname":"carol"}─▶│
  │                            (server: nickname matches disconnected carol,
  │                             restores her player object, binds new fd)
  │◀──── joined_room (reconnect:true, full snapshot) ─┤
  │                            (broadcasts to others:)
  │◀──── player_reconnected ─────────────────┤
  │◀──── game_state ─────────────────────────┤
```

**Flow C — Timeout after disconnect (no reconnect):**

```
  carol's TCP dropped at T
  at T+15s, no reconnect:
      - mark carol as folded for current hand
      - if everyone-else situation changed (only 1 active left): hand ends
      - broadcast player_left {reason:"timeout"}
      - broadcast game_state
      - if 1 player has chips remaining at hand_over, transition to GAME_OVER
```

---

## 5. Indian Poker Game Module

### 5.1 Constants

These are the concrete numeric parameters chosen for the MVP. They are easy to tune; none are load-bearing on architecture.

| Constant | Value |
|:---------|:------|
| Min players | 2 |
| Max players | 4 |
| Deck | Ten cards numbered 1–10, reshuffled every hand |
| Starting chips per player | 100 |
| Ante per hand | 5 |
| Minimum raise increment | 5 |
| Grace period on disconnect | 15 seconds |
| Delay between hands | 5 seconds (or until all players send `ready_next_hand`) |

### 5.2 State machine

```
                     +----------+
                     |  LOBBY   |
                     +----------+
                          |
                          | host sends start_game (>=2 players)
                          v
                     +-----------+
                     |  DEALING  |   (transient, immediately advances)
                     +-----------+
                          |
                          | shuffle deck, deal 1 card per active player,
                          | deduct ante from each into pot,
                          | set starting player (rotates each hand)
                          v
                     +-----------+
                     |  BETTING  |<---+
                     +-----------+    |
                       |       |      | valid actions advance turn;
                       |       |      | folded or all-in players are skipped
                       |       +------+
                       |
                       | round ends when:
                       |   (a) every still-active player has bet_this_round == current_bet
                       |       (or is all-in), OR
                       |   (b) only one non-folded player remains
                       |
                       | case (b) -> skip SHOWDOWN, jump straight to HAND_OVER
                       v
                     +-----------+
                     | SHOWDOWN  |   (transient)
                     +-----------+
                          |
                          | reveal non-folded cards,
                          | determine winner (highest card; tie splits pot evenly,
                          | remainder to earliest seat)
                          v
                     +-----------+
                     | HAND_OVER |
                     +-----------+
                          |
                          | await all ready_next_hand or 5s timeout
                          | apply elimination (chips == 0)
                          |
                     +----+---------------+
                     |                    |
                     v                    v
                 >=2 with chips        1 with chips
                     |                    |
                     v                    v
                 DEALING               GAME_OVER
                                           |
                                           v
                                       (room closes)
```

### 5.3 Action validation

- **`check`**: valid only if `player.bet_this_round == current_bet`. No chip movement.
- **`raise` {amount}**:
  - `amount` is interpreted as the **new total bet for this round** (not the delta).
  - Must satisfy `amount >= current_bet + min_raise`, **except** for an all-in raise where the player puts all their remaining chips in, which is allowed even if below that floor.
  - Must satisfy `amount - player.bet_this_round <= player.chips` (the diff is what they actually pay).
  - On valid raise: `player.chips -= (amount - player.bet_this_round)`, `player.bet_this_round = amount`, `current_bet = amount`, pot increases accordingly.
  - `current_bet` changing resets the "everyone has matched" check — all other active players must now match the new `current_bet` or fold.
- **`fold`**: set `player.status = folded`. Forfeits all chips already in the pot for this round.

### 5.4 Winner determination

- Only players whose `status == active` at showdown are considered.
- The highest `card` value wins.
- Ties: pot is split evenly. Any remainder chip (from integer division) goes to the tied player with the earliest seat position, where seat position is determined by join order.

### 5.5 Starting player rotation

- For hand 1, the starting player is the host.
- For subsequent hands, the starting player advances by one seat, skipping eliminated players.

### 5.6 Unit testability

The `IndianPoker` class is designed so that a test can:

1. Construct an initial state (4 players, fixed starting chips).
2. Feed a deterministic deck (via a seedable RNG or direct injection).
3. Call `handle_action(player_id, action)` in sequence.
4. Assert on the resulting state and on the outgoing messages returned.

No sockets, no I/O, no threads. The entire game module can be exercised in milliseconds by unit tests.

---

## 6. Room Lifecycle, Disconnect, and Reconnect

### 6.1 Room states

| State | Description |
|:------|:------------|
| `OPEN` | Room exists, game has not started (LOBBY). Accepts new joins. |
| `LOCKED` | Game has started (DEALING or beyond). Accepts only reconnects of already-joined nicknames. |
| `CLOSED` | Game is over, or all players gone. Marked for cleanup. |

### 6.2 Room code generation

- 6 characters, drawn from the **32-character confusion-free alphabet**:
  `ABCDEFGHJKLMNPQRSTUVWXYZ23456789`
  (excludes `I`, `O`, `0`, `1` to avoid misreads when shared verbally or by chat).
- On room creation, generate a random code and check for collision with existing rooms; regenerate if collision. With 32⁶ ≈ 1.07 × 10⁹ codes and at most a few thousand concurrent rooms, collisions are effectively impossible.
- Room code is returned to the host in `room_created` and is visible in every `game_state` message's top-level `room_code` field, so every client can always display it.

### 6.3 Player identification

- Identification key: `(room_code, nickname)`. No tokens, no sessions beyond this pair.
- Nicknames must be unique **within a room**. Global uniqueness is not required.
- Nickname validation: 1–16 characters, no leading/trailing whitespace, no control characters. ASCII letters/digits and Korean letters (가–힣) are permitted.

### 6.4 Join / reconnect decision flow

This is the flow executed when the server receives `join_room {room_code, nickname}`:

```
START
  |
  v
Does room with this code exist?
  NO  -> error ROOM_NOT_FOUND
  YES -> continue
  |
  v
Is this nickname currently held by a CONNECTED player in this room?
  YES -> error NICKNAME_TAKEN
  NO  -> continue
  |
  v
Is this nickname currently held by a DISCONNECTED player within the 15s grace window?
  YES -> RECONNECT PATH:
         - Bind the new Connection to the existing Player object
         - Cancel the grace-period timer for this player
         - Broadcast player_reconnected
         - Send joined_room (reconnect=true) with full game_state
         - DONE
  NO  -> continue
  |
  v
Is the room in LOCKED state (game in progress)?
  YES -> error ROOM_IN_PROGRESS
  NO  -> continue
  |
  v
Is the room at max_players?
  YES -> error ROOM_FULL
  NO  -> continue
  |
  v
Validate nickname format.
  FAIL -> error NICKNAME_INVALID
  OK   -> continue
  |
  v
NEW JOIN PATH:
  - Create Player object
  - Add to room's player list at the next seat index
  - Send joined_room (reconnect=false) with current lobby state
  - Broadcast lobby_update
  - DONE
```

This flow enforces the key invariants the user specified:

1. A disconnected player's seat can only be reclaimed by the exact same nickname.
2. Someone with a new nickname cannot slide into an empty seat during the grace window (blocked by `ROOM_IN_PROGRESS` check, because once the game is locked, only reconnections pass the earlier check).
3. Nickname collisions are checked before any state mutation.

### 6.5 Disconnect handling

When a TCP connection closes (socket `recv` returns 0 or error, or a send fails):

1. Mark the corresponding `Player.connection_status = disconnected_at(now)`.
2. If the room is in `OPEN` state:
   - If the player is the host, promote the next player in seat order to host. If no players remain, mark the room `CLOSED`.
   - Broadcast `lobby_update`.
3. If the room is in `LOCKED` state:
   - Freeze the player at their current game state; do not advance their turn if it is theirs.
   - Broadcast `player_disconnected` and an updated `game_state` (`connected: false`).
   - The event loop records this player in a "grace period" watchlist.

Each tick, the event loop scans the watchlist. For any entry where `now - disconnected_at > 15s`, apply the timeout handler:

- In `OPEN`: remove the player from the room.
- In `LOCKED`:
  - Mark their current-hand status as `folded`.
  - Broadcast `player_left {reason: "timeout"}`.
  - Check BETTING round end conditions — the timeout may have caused either a normal round-end or the "only one active left" fast-path.
  - Broadcast a new `game_state`.

If the player reconnects before the timeout, their watchlist entry is cancelled and no timeout handler runs.

### 6.6 Host role

- Host is whichever player created the room.
- Host is the only player allowed to send `start_game`, and only while in `LOBBY`.
- If the host leaves `LOBBY` (voluntarily or by timeout), the next player in seat order becomes the new host, and a `lobby_update` is broadcast.
- After `start_game`, the host role is inert and never checked again for the remainder of the session.

### 6.7 Voluntary leave

`leave_room` is handled as an immediate variant of the disconnect/timeout path:

- In `OPEN`: the player is removed from the room's player list. If they were the host, the host role is promoted as in §6.6. If the room becomes empty, it transitions to `CLOSED`. `lobby_update` is broadcast.
- In `LOCKED`: the player is marked `folded` for the current hand and then removed from future hands (as if `eliminated`). `player_left {reason: "voluntary"}` is broadcast. The BETTING round-end check runs immediately, which may trigger `SHOWDOWN` or a fast-path hand end.
- The sender's WebSocket connection is not closed by the server; the client may then close it on its side.

### 6.8 Room cleanup

- `CLOSED` room: deleted once all clients have received the final `game_over` or `lobby_update` (i.e., on the next tick after state transition).
- `OPEN` room with zero connected players: deleted after 60 seconds.
- `LOCKED` room where every player is disconnected: deleted after 60 seconds.

---

## 7. Security and Trust Model

### 7.1 Trust model

This is a small game intended for friends who already trust each other via out-of-band channels. The room code is the shared secret; anyone with the code may join. There is no account system, no identity verification beyond "the nickname you claimed". This is deliberate and appropriate for the scope.

### 7.2 Origin validation

- The WebSocket handshake `Origin` header is checked against an allowlist:
  - `https://<project>.vercel.app` (production frontend)
  - `http://localhost:<dev-port>` (local development)
- Missing or non-matching `Origin` → respond with HTTP 403 and close the TCP connection. No `ORIGIN_NOT_ALLOWED` JSON error, because the WebSocket isn't open yet.

### 7.3 Rate limiting

- Keyed by client IP.
- Limit: at most 10 of `{create_room | join_room}` attempts per IP per minute.
- Exceeding the limit: server responds with an `error` message (`RATE_LIMITED`) and closes the connection.
- Rate limits are in-memory only; they reset on server restart. Acceptable for MVP.

### 7.4 Nickname validation

- Length 1 to 16 code points.
- Allowed characters: ASCII letters, digits, underscore, hyphen, space (not at start or end), and Korean syllables in the range 가–힣.
- Disallowed: control characters, anything that would render ambiguously (zero-width, bidi overrides, etc.).
- Invalid nicknames receive `NICKNAME_INVALID` before any state is mutated.

### 7.5 Message size limit

- Any single WebSocket message larger than 4 KB is rejected with WebSocket close code `1009 Message Too Big`, and the connection is closed.
- No legitimate game message is anywhere close to this limit; the cap exists to make DoS via oversized frames harder.

### 7.6 Persistence

- There is none. All state is in-memory. A server restart clears every room.
- Acceptable because sessions are short (tens of minutes at most) and there is no data worth persisting.

---

## 8. Error Handling

Errors are handled at the layer they originate in, not bubbled up unstructured.

### 8.1 Layer 1: Protocol errors

- Malformed WebSocket frame, bad masking, reserved bits set, unknown opcode → close with WebSocket close code `1002 Protocol Error`.
- Oversized payload → close with `1009 Message Too Big`.
- HTTP Upgrade handshake failure (missing headers, bad version, bad Origin) → respond with `HTTP/1.1 400 Bad Request` or `403 Forbidden` and close TCP.

### 8.2 Layer 2: Session errors

- Unknown `type` field, missing required fields, JSON parse errors → send JSON `error` message with code `MALFORMED_MESSAGE`, keep connection open.
- Room/join errors (room not found, nickname taken, room full, room in progress) → send `error` with appropriate code, keep connection open.

### 8.3 Layer 3: Game errors

- Action out of turn, invalid action for phase, invalid raise amount → send `error` with appropriate code, **do not** advance state, keep connection open.

### 8.4 Internal errors

- Any C++ exception that escapes a handler → log with stack context if possible, send `error {code: "INTERNAL_ERROR"}` to the originating connection, close that connection, continue running the rest of the event loop. **The server process does not crash on a single bad connection.**

---

## 9. Testing Strategy

Testing is planned in four layers, roughly from cheapest to most expensive:

### 9.1 Unit tests: protocol parsing (no sockets)

- WebSocket frame decoder tested against every example frame in RFC 6455 § 5.7 and additional hand-crafted edge cases (multi-fragment, masked, control-frame-interleaved, payload lengths at the 125/126/127 boundaries).
- WebSocket frame encoder tested by round-tripping through the decoder.
- HTTP Upgrade request parser tested with valid and malformed requests, including variations in header casing, whitespace, and missing fields.
- SHA-1 + base64 wrappers verified with the known example from RFC 6455 § 1.3:
  `dGhlIHNhbXBsZSBub25jZQ==` + magic → `s3pPLMBiTxaQ9kYGzzhZRbK+xOo=`.

### 9.2 Unit tests: game logic (no sockets)

- Construct `IndianPoker` states in isolation.
- Inject deterministic card draws.
- Feed action sequences and assert on resulting state and outgoing messages.
- Cases to cover:
  - Normal 2-player hand (check/raise/call/reveal).
  - 3-player hand with one fold, two to showdown.
  - All-but-one-fold fast path (skip SHOWDOWN).
  - Tie splitting with remainder.
  - Elimination on a losing hand.
  - GAME_OVER when one player remains.
  - Turn skipping when a player is folded.
  - Disconnect mid-hand translated into fold on timeout.

### 9.3 Integration tests: real sockets on localhost

- Spawn the server in a background thread or subprocess.
- Use a minimal C++ test WebSocket client (can reuse our own encoder/decoder) to simulate 2–4 players.
- Walk through: create room, join, start, full hand, reconnect, game over.

### 9.4 Manual testing

- Two browser tabs on the dev machine against `localhost`.
- Two devices (laptop + phone) against the Cloudflare Tunnel URL.
- Intentional WiFi drops to exercise the reconnect path in real network conditions.

---

## 10. Deployment Architecture

### 10.1 Topology

```
+-----------------+        HTTPS        +-------------+
|  User browser   | ------------------> | Vercel CDN  |
+-----------------+    (static assets)  +-------------+
        |
        | page loaded, JS runs:
        | new WebSocket("wss://game.example.com/")
        |
        v
+------------------+   TLS terminated   +----------------------+
| Cloudflare Edge  | ------------------> | cloudflared (local)  |
+------------------+                     +----------------------+
                                                   |
                                                   | plain TCP/WebSocket
                                                   v
                                         +------------------------+
                                         |  C++ server (local)    |
                                         |  listening on 127.0.0.1|
                                         +------------------------+
```

### 10.2 Backend deployment

- Runs as a plain binary on the developer's local machine.
- Configuration (listen port, Origin allowlist, log level) read from environment variables.
- `cloudflared` is preconfigured to tunnel a subdomain of the user's Cloudflare domain to `localhost:PORT`.
- No container, no systemd unit for the MVP; started manually during play sessions.

### 10.3 Frontend deployment

- The `client/` directory is deployed to Vercel as static files.
- The WebSocket URL (`wss://game.example.com/`) is baked into the JS as a constant, or read from a small `config.js` that is swapped per environment.

### 10.4 CI/CD

None for the MVP. Deployment is `git push` for Vercel and manual run for the backend.

---

## 11. Planned Directory Layout

```
indian-poker/
├── client/                   # Static frontend, deployed to Vercel
│   ├── index.html            # Landing page: create / join room
│   ├── css/
│   │   └── style.css
│   └── js/
│       ├── main.js           # Entry point; page routing (lobby vs game)
│       ├── ws_client.js      # Thin wrapper around WebSocket, JSON dispatch
│       └── ui.js             # Render game_state -> DOM
├── server/                   # C++ server
│   ├── CMakeLists.txt
│   ├── main.cpp
│   ├── net/                  # Layer 1
│   │   ├── event_loop.{h,cpp}
│   │   ├── tcp_listener.{h,cpp}
│   │   ├── connection.{h,cpp}
│   │   ├── ws_handshake.{h,cpp}
│   │   └── ws_frame.{h,cpp}
│   ├── session/              # Layer 2
│   │   ├── player.{h,cpp}
│   │   ├── room.{h,cpp}
│   │   ├── room_registry.{h,cpp}
│   │   └── router.{h,cpp}
│   ├── game/                 # Layer 3
│   │   ├── igame.h           # Interface only
│   │   └── indian_poker/
│   │       ├── indian_poker.{h,cpp}
│   │       └── state.{h,cpp}
│   └── util/
│       ├── json.{h,cpp}      # Thin wrapper around nlohmann/json
│       ├── base64.{h,cpp}    # Wrapper over OpenSSL EVP base64
│       └── sha1.{h,cpp}      # Wrapper over OpenSSL SHA-1
├── tests/
│   ├── test_ws_frame.cpp
│   ├── test_ws_handshake.cpp
│   ├── test_indian_poker.cpp
│   └── test_helpers/
├── docs/
│   └── protocol-examples.md  # Annotated example frames, for debugging
├── PROJECT.md                # This file
├── PROJECT.ko.md
├── README.md
├── README.ko.md
├── LICENSE
└── .gitignore
```

---

## 12. Implementation Milestones

Each milestone ends in a runnable state (even if the runnable thing is small) and a commit. Do not skip ahead; the sequence is chosen so that each step can be verified before the next adds complexity.

| # | Milestone | Runnable result |
|:--|:----------|:----------------|
| M1 | Minimal TCP echo server with `select()` | `telnet localhost PORT` echoes back, handles multiple connections |
| M2 | HTTP Upgrade handshake | Browser `new WebSocket(...)` successfully opens a connection (doesn't disconnect on 400) |
| M3 | WebSocket text frames (no fragmentation) | Browser echoes text roundtrip; verified with masked client frames |
| M4 | Fragmentation + control frames (ping/pong/close) | Browser's idle ping/pong is handled; large messages split across fragments work |
| M5 | Session-layer scaffolding: Player, Room, RoomRegistry | `create_room` / `join_room` work, lobby updates broadcast |
| M6 | Lobby flow with room-code generation, nickname validation | Two browser tabs can join the same room and see each other in the lobby |
| M7 | Disconnect detection + 15s reconnect window | Close a tab → other players see `player_disconnected`; reopen with same nickname → restored |
| M8 | `IndianPoker` game logic, unit tested in isolation | `ctest` runs the full game suite with no server running |
| M9 | Wire game logic into session layer: `start_game`, `action`, `game_state` | A full hand can be played between two tabs |
| M10 | `HAND_OVER`, next hand, elimination, `GAME_OVER` | A full game session runs end to end |
| M11 | Client UI (bare-bones HTML/CSS/JS) | Game is actually playable in a browser, not just via devtools |
| M12 | Security polish: Origin check, rate limit, message size cap, nickname rules | Server rejects bad clients cleanly |
| M13 | Deploy: Vercel for the frontend, Cloudflare Tunnel for the backend | First real playtest with a friend on a phone over the public internet |

---

## 13. Open Questions and Future Work

Not in scope for the MVP, but worth noting for later:

- **Second game as an `IGame` module.** Liar Game or a simple card matching game would validate that the three-layer separation actually works.
- **Spectator mode.** Eliminated players currently leave the session; they could instead be demoted to spectators who still receive `game_state` but cannot act.
- **Turn timer.** Current design has no timer on a player's turn. A 30-second turn clock would prevent stalling.
- **Server-side logging / metrics.** A simple line-per-message log to a file would be useful for debugging protocol issues during playtests.
- **Reconnect across full browser restart.** Would require persisting something beyond `sessionStorage`, likely `localStorage`, plus accepting a small impersonation risk.
- **Audit of the allowed nickname regex.** Unicode is a rabbit hole; revisit once we see what friends actually type.
- **Dockerizing the backend** to make deploying to a small VPS (instead of a laptop + tunnel) straightforward.
- **TLS inside the C++ server.** Replacing Cloudflare's TLS termination with in-process TLS (e.g., via OpenSSL or mbedTLS) would be a logical next step if this project continues.

---

*End of design document.*
