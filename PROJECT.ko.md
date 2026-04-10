# 인디언포커 — 디자인 문서

**상태:** 설계 확정, 구현 시작 전.
**최종 갱신:** 2026-04-11
**작성자:** 박철원
**도움:** Claude (Anthropic) — 브레인스토밍 및 설계 리뷰 파트너로 활용.

---

## 목차

1. [개요 및 목표](#1-개요-및-목표)
2. [아키텍처](#2-아키텍처)
3. [기술 선택](#3-기술-선택)
4. [애플리케이션 프로토콜](#4-애플리케이션-프로토콜)
5. [인디언포커 게임 모듈](#5-인디언포커-게임-모듈)
6. [방 수명 · 연결 끊김 · 재접속](#6-방-수명--연결-끊김--재접속)
7. [보안 및 신뢰 모델](#7-보안-및-신뢰-모델)
8. [에러 처리](#8-에러-처리)
9. [테스트 전략](#9-테스트-전략)
10. [배포 아키텍처](#10-배포-아키텍처)
11. [디렉토리 구조 (예정)](#11-디렉토리-구조-예정)
12. [구현 마일스톤](#12-구현-마일스톤)
13. [미해결 질문 및 향후 작업](#13-미해결-질문-및-향후-작업)

---

## 1. 개요 및 목표

### 1.1 이 프로젝트가 무엇인가

2~4 명이 6 자리 방 코드로 방에 모여 여러 핸드를 겨루는, 브라우저 기반 실시간 멀티플레이어 인디언포커 게임. 학습 가치를 위해 서버와 WebSocket 프로토콜을 C++로 바닥부터 직접 작성하며, 클라이언트는 의도적으로 최소한의 바닐라 JS 로만 구성한다.

### 1.2 학습 목표 (진짜 목적)

이 프로젝트의 일차 목적은 "잘 다듬어진 제품을 출시" 하는 것이 아니다. 목표의 우선순위는 다음과 같다:

1. **raw TCP 위에서 진짜 애플리케이션 레벨 프로토콜을 설계하고 구현해본다.** 메시지 포맷, 프레임 경계, 제어 흐름, 상태 동기화를 — 교과서의 추상 개념이 아니라 — 실제 바이트 단위로 설계한다는 것이 무엇인지 몸으로 이해한다.
2. **WebSocket 프로토콜(RFC 6455)을 직접 구현한다.** HTTP Upgrade 핸드셰이크, 클라이언트-서버 마스킹이 포함된 프레임 파싱, 프래그멘테이션, 컨트롤 프레임(ping/pong/close) 까지 포함.
3. **I/O 멀티플렉싱에 `select(2)` 를 쓴다.** 스레드 없이 단일 스레드 이벤트 루프로 여러 클라이언트를 다루면서, 현재 운영체제 수업에서 배우고 있는 동시성 모델을 복습 겸 실전 적용한다.
4. **네트워크 레이어와 게임 로직을 깨끗하게 분리해서, 이후에 다른 게임을 붙이기 쉽게 만든다.** 게임 로직은 소켓 없이 단위 테스트가 가능해야 한다.
5. **친구들과 실제로 해볼 수 있는 동작하는 결과물을 만든다.** Cloudflare Tunnel + Vercel 파이프라인 위에서 — 열 겹의 프레임워크 마법 뒤에 숨은 채가 아니라 — 내가 end-to-end 로 이해한 상태에서.

### 1.3 명시적 비목표 (scope creep 방지)

- **본격적인 포커 게임이 아님.** 사이드팟 없음, 멀티 라운드 베팅 없음, 블라인드 구조 없음.
- **MVP 에서는 미니게임 플랫폼이 아님.** 인디언포커 하나만 구현한다. 아키텍처가 "여러 게임을 붙일 여지를 남김" 이라는 뜻이지, "여러 게임을 구현함" 이라는 뜻이 아님.
- **RFC 6455 전체 구현은 아님.** 바이너리 프레임, 확장(extensions), permessage-deflate 는 명시적으로 제외.
- **C++ 에서 TLS 는 하지 않음.** C++ 서버는 평문 WebSocket 만 다루고, TLS 는 Cloudflare 엣지에서 종단된다.
- **계정 시스템 없음.** 로그인, 사용자 DB, 매치 히스토리 전부 없음. 상태는 서버 메모리에만 있음.
- **본격적인 프런트엔드 완성도 추구 안 함.** 바닐라 JS, 최소한의 HTML/CSS, 프런트 프레임워크 없음, 빌드 파이프라인 없음.
- **운영급 신뢰성 목표 아님.** 클러스터링 없음, 무중단 재시작 없음, 다중 서버 지원 없음.

---

## 2. 아키텍처

### 2.1 서버의 3-계층 구조

서버는 세 개의 엄격히 분리된 모듈로 나뉜다. 각 레이어는 오직 아래 레이어의 API에만 의존한다. 이 분리는 이 프로젝트에서 가장 중요한 구조적 결정이다.

```
+------------------------------------------------------------+
|  Layer 3: Game Logic                                       |
|    - IGame 인터페이스                                      |
|        start(players)                                      |
|        handle_action(player_id, action) -> Outgoing        |
|        snapshot_for(player_id) -> StateView                |
|        on_player_disconnect(player_id) -> Outgoing         |
|    - IndianPoker : IGame                                   |
+------------------------------------------------------------+
|  Layer 2: Session / Room Infrastructure                    |
|    - Player (닉네임, 칩, 연결 상태)                        |
|    - Room (코드, 플레이어 리스트, 호스트, IGame 인스턴스)  |
|    - RoomRegistry (생성/검색/정리)                         |
|    - Router (어떤 Connection 들에게 메시지를 보낼지 결정)  |
+------------------------------------------------------------+
|  Layer 1: Network / Protocol                               |
|    - EventLoop (select fd_set, tick 기반)                  |
|    - TCPListener                                           |
|    - Connection (TCP 소켓 하나, WS 프레이밍 상태)          |
|    - WebSocketHandshake                                    |
|    - WebSocketFrame (디코드/인코드, 마스킹, 프래그먼트)    |
+------------------------------------------------------------+
```

### 2.2 격리 원칙

다음 원칙들은 단순한 "좋은 관행" 이 아니라, "어기면 학습 목표 자체가 무너지는" 제약이다:

- **Layer 1 은 "방" 이나 "플레이어" 가 무엇인지 모른다.** 완성된 텍스트 메시지 한 건(필요하다면 프래그먼트를 재조립한)을 상위 레이어 콜백에 올려 보내고, 나가는 메시지 하나를 받아 송신한다. 이것이 Layer 1 의 전부다.
- **Layer 2 는 게임 메시지나 게임 규칙을 파싱하지 않는다.** `type` 필드를 보고, 필요한 경우 방의 게임 인스턴스에 라우팅하며, 게임이 뱉는 메시지를 올바른 플레이어 집합에게 브로드캐스트할 뿐이다.
- **Layer 3 은 `send()` 를 부르지 않고, 파일 디스크립터를 만지지 않으며, WebSocket 을 모른다.** 게임 상태와 액션을 받아 새로운 상태와 `Outgoing` 레코드 리스트를 반환한다. 각 레코드는 `{target: "broadcast" | player_id, payload: JSON}` 형태. 이걸 실제 네트워크 송신으로 변환하는 건 Layer 2 의 몫이다.

이 마지막 원칙이 게임 로직의 단위 테스트 가능성을 보장한다 — 소켓 없이 상태를 만들고, `handle_action` 을 호출하고, 반환된 새 상태와 outgoing 메시지만 검증하면 된다.

### 2.3 상태 동기화 모델

서버-클라이언트 상태 동기화는 **플레이어별 리댁션이 적용된 전체 상태 브로드캐스트** 방식을 쓴다. 이벤트 소싱이 아니다.

근거:

- 상태를 바꾸는 액션이 처리된 뒤, 서버는 새 게임 상태를 계산한 다음 각 플레이어를 위한 뷰(`snapshot_for(player_id)`)를 구성한다. 이 뷰는 숨겨진 정보(예: 해당 플레이어 본인의 카드)를 `null` 로 가린 형태. 이를 각 연결된 플레이어에게 `game_state` 메시지로 단건 전송한다.
- 클라이언트는 게임 상태를 로컬에서 계산하지 않는다. `game_state` 스냅샷을 받고 그대로 렌더링할 뿐. "서버가 진실의 유일한 원천" 이라는 원칙이 구조적으로 강제된다 — 클라이언트에 divergence 를 일으킬 로직 자체가 없으므로.
- 재접속 처리가 자동으로 풀린다: 재접속 시 그냥 최신 `game_state` 스냅샷 하나를 보내주면 된다. 이벤트 리플레이도, 상태 재구성도 필요 없다.
- 메시지 크기는 매우 작다(수백 바이트). 인디언포커의 상태 자체가 작기 때문. 대역폭은 고려 사항이 아니다.

### 2.4 클라이언트 구조

클라이언트는 의도적으로 미니멀하다:

- 정적 HTML + CSS + 바닐라 JS. React/Vue/Svelte 없음, 빌드 단계 없음, Vercel 이 정적 파일을 서빙하는 데 필요한 것 이상의 npm 의존성 없음.
- 세션당 WebSocket 하나. 첫 사용자 액션 시점에 열어서 페이지가 닫힐 때까지 재사용.
- 상태는 "가장 최근에 도착한 `game_state` 메시지가 말하는 것" 이 전부. UI 는 그걸 보고 그린다. 새 `game_state` 가 오면 DOM 을 다시 그린다(풀 업데이트).
- `sessionStorage` 는 실수로 새로고침했을 때 현재 방 코드를 기억하는 용도로만 사용한다. F5 한 번에 자리가 사라지지 않도록.

---

## 3. 기술 선택

### 3.1 서버

| 관심사 | 선택 | 근거 |
|:------|:----|:----|
| 언어 | C++ (C++17 이상) | 사용자 경험과 일치(C++ Practice 과목), raw 소켓 API 를 자연스럽게 노출, GC 걱정 없음 |
| 빌드 시스템 | CMake | 현대 C++ 에서 표준, clangd/VS Code 와 쉽게 통합 |
| 소켓 | POSIX BSD (`<sys/socket.h>`, `<netinet/in.h>`) | macOS(개발)와 Linux(배포)에서 동일 API, 추상화 레이어 없음 |
| 동시성 | 단일 스레드 `select(2)` 이벤트 루프 | 현재 OS 수업 주제, 락이 전혀 필요 없음, 수십 개 연결에 충분 |
| WebSocket | RFC 6455 직접 구현 | **프로젝트의 핵심.** 의도적으로 라이브러리 안 씀 |
| 암호 | OpenSSL (SHA-1 + base64 만) | SHA-1 직접 구현은 주제 밖(암호학, 네트워크 아님); 래핑은 5줄 |
| JSON | [nlohmann/json](https://github.com/nlohmann/json) 단일 헤더 | JSON 라이브러리 선택은 학습 포인트가 아님; nlohmann 은 header-only 에 사용감 좋음 |

### 3.2 클라이언트

| 관심사 | 선택 | 근거 |
|:------|:----|:----|
| 언어 | 바닐라 JavaScript (ES2022) | 프레임워크 세금 없음, DOM 과 상태를 직접 다루게 강제 |
| WebSocket | 브라우저 기본 `WebSocket` API | 클라이언트 쪽 핸드셰이크는 브라우저가 알아서 해줌 |
| 마크업/스타일 | HTML + 직접 작성한 CSS | 이 프로젝트는 네트워크에 관한 것이지 프런트엔드에 관한 것이 아님 |
| 상태 유지 | `sessionStorage` | 새로고침엔 살아남고 탭 종료엔 사라짐 — 15초 재접속 의도와 일치 |

### 3.3 배포

| 컴포넌트 | 서비스 | 근거 |
|:--------|:------|:----|
| 정적 프런트 | Vercel | 빠른 CDN, git push 배포, 무료 |
| TLS 종단 + 공개 | Cloudflare Tunnel (`cloudflared`) | 사용자가 이미 Cloudflare 도메인 소유, 터널 무료, 노트북에서 돌리면서 포트 포워딩 없이 공개 가능 |
| 백엔드 런타임 | 로컬 머신 (macOS 개발, Linux 호환) | 바이너리, MVP 에 컨테이너 런타임 불필요 |

### 3.4 명시적으로 거절한 것들

- **Socket.IO / 모든 WebSocket 라이브러리.** 프로토콜을 직접 구현하는 게 목적이다.
- **`epoll` / `kqueue`.** 플랫폼별 분기가 생김; `select` 가 현재 OS 수업의 해당 단원이고, 이 규모엔 그 이상이 필요 없음.
- **Thread-per-connection.** OS 수업과의 접점을 놓치게 됨; 이 규모엔 불필요.
- **React / SPA 프레임워크.** 학습 목표와 무관하고, 빌드 단계를 추가하며 DOM 작업을 가림.
- **로컬 개발용 Docker.** 솔로 프로젝트엔 `cmake && make` 가 더 빠르고 단순함.
- **C++ 내 TLS 구현.** TLS 만으로도 한 학기짜리 프로젝트. Cloudflare 가 종단한다.
- **DB / Redis / 영속 저장소.** MVP 에 영속성 요구사항이 없음. 서버 재시작이 모든 방을 끝냄 — 허용 가능.

---

## 4. 애플리케이션 프로토콜

### 4.1 전송

모든 애플리케이션 메시지는 **UTF-8 로 인코딩된 JSON 텍스트** 이며, WebSocket **텍스트 프레임** (opcode `0x1`) 에 실려 전달된다. JSON 객체 하나 = 논리적 메시지 하나. 메시지가 WebSocket 레이어에서 프래그먼트로 쪼개질 수는 있지만, Layer 2 가 볼 시점엔 JSON 객체가 Layer 1 에서 이미 재조립된 상태여야 한다.

모든 메시지는 최상위에 `type` 필드를 가진다. 알 수 없는 `type` 은 `MALFORMED_MESSAGE` 로 거절한다.

### 4.2 클라이언트 → 서버 메시지

```json
// 새 방을 생성한다. 이 요청을 보낸 사람이 호스트가 된다.
{"type": "create_room", "nickname": "alice", "max_players": 4}

// 기존 방에 입장한다. 신규 입장과 재접속 양쪽에 쓴다;
// 서버가 현재 방 상태를 보고 어느 쪽인지 판단한다.
{"type": "join_room", "room_code": "AB7K92", "nickname": "alice"}

// 호스트만, LOBBY 단계에서만, 최소 2명 이상일 때만 유효.
{"type": "start_game"}

// 게임 액션. BETTING 단계의 자기 차례에만 유효.
{"type": "action", "action": "check"}
{"type": "action", "action": "raise", "amount": 20}
{"type": "action", "action": "fold"}

// 핸드 종료 화면을 확인했고 다음 핸드 준비됐음을 알림.
{"type": "ready_next_hand"}

// 자발적 퇴장.
{"type": "leave_room"}
```

### 4.3 서버 → 클라이언트 메시지

```json
// create_room 직접 응답.
{
  "type": "room_created",
  "room_code": "AB7K92",
  "you": "alice",
  "host": "alice"
}

// join_room 직접 응답.
// "reconnect" 가 true 이면 "alice" 자리에 복귀한 재접속.
{
  "type": "joined_room",
  "room_code": "AB7K92",
  "you": "alice",
  "reconnect": false,
  "state": { /* 아래 game_state 와 동일한 형태 */ }
}

// 로비 플레이어 리스트나 호스트가 바뀔 때 브로드캐스트.
{
  "type": "lobby_update",
  "room_code": "AB7K92",
  "players": ["alice", "bob"],
  "host": "alice",
  "max_players": 4
}

// 핵심 메시지. 상태를 바꾸는 이벤트가 일어날 때마다 방 안의 모든 연결된
// 플레이어에게 각자 자기 관점의 뷰가 전송된다 (자기 카드는 가려진 채).
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

`game_state` 에서의 카드 가시성 규칙:

- 각 `player` 항목의 `card` 는 **이 메시지를 받는 플레이어가** 볼 수 있는 값이다.
- 자기 자신의 카드는 항상 `null` (단, `SHOWDOWN` 과 `HAND_OVER` 단계에서는 예외).
- 폴드한 플레이어의 카드는 모두에게 `null` (역시 `SHOWDOWN`/`HAND_OVER` 예외).
- 아직 카드를 받지 않은 상태(예: `LOBBY`)에서는 `card` 가 `null`.

플레이어 `status` 값: `active` (이번 핸드 생존), `folded` (이번 핸드 폴드), `eliminated` (칩 0, 게임 탈락), `disconnected_grace` (15초 재접속 윈도우 안 — 해당 플레이어 기준 게임이 일시 정지).

```json
// 각 핸드가 끝날 때 브로드캐스트. 여기서는 카드가 완전히 공개된다.
{
  "type": "hand_over",
  "hand_number": 3,
  "winner": "bob",
  "pot_won": 50,
  "revealed": {"alice": 5, "bob": 9, "dave": 2},
  "eliminated_after_this_hand": [],
  "next_hand_starts_at_ms": 5000
}

// 게임 세션이 끝날 때 브로드캐스트 (칩이 남은 플레이어가 1명일 때).
{
  "type": "game_over",
  "winner": "bob"
}

// 플레이어의 TCP 연결이 끊겼을 때 브로드캐스트.
// 핸드는 즉시 일시 정지되고, 해당 플레이어에 대한 타이머가 시작된다.
{
  "type": "player_disconnected",
  "nickname": "carol",
  "grace_period_ms": 15000
}

// 플레이어가 윈도우 안에 재접속에 성공했을 때 브로드캐스트.
{
  "type": "player_reconnected",
  "nickname": "carol"
}

// 플레이어가 영구적으로 제거될 때 브로드캐스트.
// "reason" 은 "timeout" 또는 "voluntary".
{
  "type": "player_left",
  "nickname": "carol",
  "reason": "timeout"
}

// 실패한 클라이언트 메시지에 대한 직접 응답.
// 브로드캐스트 아님.
{
  "type": "error",
  "code": "NOT_YOUR_TURN",
  "message": "It's bob's turn, not yours."
}
```

### 4.4 에러 코드

| 코드 | 의미 |
|:----|:----|
| `ROOM_NOT_FOUND` | 존재하지 않는 방 코드로 `join_room` |
| `ROOM_FULL` | 방이 `max_players` 에 도달한 상태에서 `join_room` |
| `ROOM_IN_PROGRESS` | 게임 시작 후 신규 플레이어(끊긴 자리 매칭 실패)가 `join_room` |
| `NICKNAME_TAKEN` | 그 방에 연결된 플레이어가 이미 쓰고 있는 닉네임 |
| `NICKNAME_INVALID` | 닉네임 검증 실패(길이, 문자) |
| `NOT_HOST` | 호스트 아닌 사람이 `start_game` 보냄 |
| `NOT_ENOUGH_PLAYERS` | 2명 미만에서 `start_game` |
| `NOT_YOUR_TURN` | 자기 차례 아닌데 `action` 보냄 |
| `INVALID_ACTION` | 현재 단계에 맞지 않는 `action` |
| `INVALID_RAISE_AMOUNT` | 레이즈 금액이 `current_bet + min_raise` 미만이거나 보유 칩 초과 |
| `MALFORMED_MESSAGE` | JSON 파싱 에러 또는 필수 필드 누락 |
| `RATE_LIMITED` | 같은 IP 에서 짧은 시간에 너무 많은 요청 |
| `ORIGIN_NOT_ALLOWED` | `Origin` 헤더 때문에 WebSocket 핸드셰이크에서 거절됨 |
| `INTERNAL_ERROR` | 예상치 못한 서버 에러 |

### 4.5 전형적인 메시지 플로우

**플로우 A — 방 생성과 게임 시작:**

```
alice                                     server
  │                                          │
  ├──── WS 핸드셰이크 (Origin 검증) ────────▶│
  │◀────── 101 Switching Protocols ──────────┤
  │                                          │
  ├── {"type":"create_room","nickname":...}─▶│
  │                           (방 생성, alice 를 호스트로 등록)
  │◀──────── room_created ───────────────────┤
  │◀──────── lobby_update ───────────────────┤  (1명, alice)
  │                                          │
  │   (bob 이 다른 브라우저에서 같은 흐름으로 입장)
  │◀──────── lobby_update ───────────────────┤  (2명, alice+bob)
  │                                          │
  ├── {"type":"start_game"}─────────────────▶│
  │                           (LOBBY → DEALING → BETTING)
  │◀──────── game_state ─────────────────────┤  (BETTING, alice 차례)
```

**플로우 B — 게임 중 끊김 후 재접속:**

```
carol                                      server
  │                                           │
  │  (carol 이 BETTING 단계에서 플레이 중)     │
  │  ╳────── TCP 연결 끊김 ──────────────────▶│
  │                            (서버가 close 감지, 15초 타이머 시작)
  │                            (같은 방에 브로드캐스트:)
  │                               player_disconnected
  │                               game_state (carol.connected=false,
  │                                            phase 그대로, 차례 그대로)
  │                                           │
  │  (8초 뒤, carol 이 새 탭을 열고 재접속 시도)
  │                                           │
  ├──── WS 핸드셰이크 ────────────────────▶│
  ├── {"type":"join_room","room_code":"AB7K92","nickname":"carol"}─▶│
  │                            (서버: 닉네임이 끊긴 carol 과 일치,
  │                             기존 Player 객체 부활, 새 fd 바인딩)
  │◀──── joined_room (reconnect:true, 전체 스냅샷) ─┤
  │                            (다른 사람에게 브로드캐스트:)
  │◀──── player_reconnected ─────────────────┤
  │◀──── game_state ─────────────────────────┤
```

**플로우 C — 끊긴 뒤 재접속 없이 타임아웃:**

```
  carol 이 T 시점에 TCP 끊김
  T+15 초에도 재접속 없음:
      - carol 을 현재 핸드에서 fold 처리
      - "한 명만 남음" 같은 조건이 바뀌었는지 확인하여 핸드 종료 여부 결정
      - player_left {reason:"timeout"} 브로드캐스트
      - game_state 브로드캐스트
      - 만약 한 핸드가 끝난 시점에 칩이 남은 플레이어가 1명이면 GAME_OVER 로 전이
```

---

## 5. 인디언포커 게임 모듈

### 5.1 상수

MVP 에 쓰는 구체 수치 파라미터. 아키텍처에 load-bearing 하지 않으므로 나중에 튜닝 가능.

| 상수 | 값 |
|:----|:----|
| 최소 인원 | 2명 |
| 최대 인원 | 4명 |
| 덱 | 1–10 숫자 카드 10장, 매 핸드 새로 셔플 |
| 시작 칩 (인당) | 100 |
| 앤티 (핸드당) | 5 |
| 최소 레이즈 증분 | 5 |
| 연결 끊김 그레이스 | 15초 |
| 핸드 간 간격 | 5초 (또는 모든 플레이어가 `ready_next_hand` 전송 시) |

### 5.2 상태 머신

```
                     +----------+
                     |  LOBBY   |
                     +----------+
                          |
                          | 호스트 start_game (≥2명)
                          v
                     +-----------+
                     |  DEALING  |   (순간적 전이)
                     +-----------+
                          |
                          | 덱 셔플, 각 active 에게 카드 1장,
                          | 앤티 차감해 팟에 적립,
                          | 시작 플레이어 결정 (매 핸드 자리가 회전)
                          v
                     +-----------+
                     |  BETTING  |<---+
                     +-----------+    |
                       |       |      | 유효한 액션은 턴을 진행;
                       |       |      | 폴드/올인 플레이어는 건너뜀
                       |       +------+
                       |
                       | 라운드 종료 조건:
                       |   (a) 생존 액티브 전원이 current_bet 에 맞춤 (또는 올인)
                       |   (b) 폴드 안 한 사람이 1명만 남음
                       |
                       | (b) 인 경우 SHOWDOWN 스킵, 바로 HAND_OVER
                       v
                     +-----------+
                     | SHOWDOWN  |   (순간적 전이)
                     +-----------+
                          |
                          | 폴드 안 한 사람들의 카드 공개,
                          | 승자 결정 (최고 카드; 동점 시 균등 분배,
                          | 나머지 1칩은 자리 순 앞 사람)
                          v
                     +-----------+
                     | HAND_OVER |
                     +-----------+
                          |
                          | 전원 ready_next_hand 또는 5초 타임아웃
                          | 탈락 적용 (칩 0)
                          |
                     +----+---------------+
                     |                    |
                     v                    v
                 ≥2 명 생존             1명만 생존
                     |                    |
                     v                    v
                 DEALING               GAME_OVER
                                           |
                                           v
                                        (방 종료)
```

### 5.3 액션 유효성 검사

- **`check`**: `player.bet_this_round == current_bet` 일 때만 유효. 칩 이동 없음.
- **`raise` {amount}**:
  - `amount` 는 이번 라운드 **총 베팅 금액 (누적, 델타 아님)** 로 해석.
  - `amount >= current_bet + min_raise` 여야 함. **예외**: 남은 칩을 모두 거는 올인 레이즈는 이 바닥보다 낮아도 허용.
  - `amount - player.bet_this_round <= player.chips` (실제 지불 금액).
  - 유효 시: `player.chips -= (amount - player.bet_this_round)`, `player.bet_this_round = amount`, `current_bet = amount`, 팟 증가.
  - `current_bet` 이 올라갔으므로 "모두 맞췄음" 조건이 초기화됨 — 다른 active 들이 새 `current_bet` 에 맞추거나 폴드해야 함.
- **`fold`**: `player.status = folded`. 이 라운드에 이미 팟에 넣은 칩은 몰수.

### 5.4 승자 결정

- 쇼다운 시점에 `status == active` 인 플레이어만 후보.
- `card` 값이 가장 큰 플레이어가 승자.
- 동점: 팟을 균등 분배. 정수 나눗셈의 나머지 1칩은 자리 순서가 가장 앞인 동점자에게.

### 5.5 시작 플레이어 로테이션

- 핸드 1: 시작 플레이어 = 호스트.
- 이후 핸드: 자리 순으로 한 칸 전진. 탈락한 플레이어는 건너뜀.

### 5.6 단위 테스트 가능성

`IndianPoker` 클래스는 테스트에서 아래 흐름을 그대로 실행할 수 있도록 설계한다:

1. 초기 상태 생성 (4명, 시작 칩).
2. 결정론적 덱 주입 (seedable RNG 또는 직접 주입).
3. `handle_action(player_id, action)` 을 순서대로 호출.
4. 결과 상태와 반환된 outgoing 메시지를 assert.

소켓 없음, I/O 없음, 스레드 없음. 전체 게임 모듈을 단위 테스트가 밀리초 단위로 돌릴 수 있다.

---

## 6. 방 수명 · 연결 끊김 · 재접속

### 6.1 방 상태

| 상태 | 설명 |
|:----|:----|
| `OPEN` | 방 존재, 게임 시작 전(LOBBY). 신규 입장 수용. |
| `LOCKED` | 게임 시작됨(DEALING 이후). 신규 입장 거절; 이미 참가한 닉네임의 재접속만 허용. |
| `CLOSED` | 게임 종료 또는 전원 이탈. 정리 대상으로 표시. |

### 6.2 방 코드 생성

- 6자, **혼동 문자 제외 32자 알파벳** 에서 추출:
  `ABCDEFGHJKLMNPQRSTUVWXYZ23456789`
  (`I`, `O`, `0`, `1` 제외 — 말이나 채팅으로 공유할 때 혼동 방지)
- 방 생성 시 랜덤 6자 생성 후 기존 방과 충돌 검사; 충돌 시 재생성. 32⁶ ≈ 10.7 억 조합이라 동시 방 몇 천 개 기준 실질적으로 충돌 불가.
- 방 코드는 `room_created` 응답에 담기고, 모든 `game_state` 메시지의 최상위 `room_code` 필드에도 실려서, 모든 클라이언트가 언제든 화면에 표시할 수 있다.

### 6.3 플레이어 식별

- 식별 키: `(room_code, nickname)`. 토큰 없음, 이 쌍을 넘어서는 세션 개념 없음.
- 닉네임은 **한 방 안에서** 유일해야 한다. 전역 유일성은 요구되지 않음.
- 닉네임 검증: 1–16 자, 앞뒤 공백 불허, 제어 문자 불허. ASCII 영숫자 및 한글 음절 (가–힣) 허용.

### 6.4 입장 / 재접속 결정 플로우

`join_room {room_code, nickname}` 을 받았을 때 서버가 실행하는 흐름:

```
시작
  |
  v
이 코드의 방이 존재?
  NO  -> 에러 ROOM_NOT_FOUND
  YES -> 계속
  |
  v
이 닉네임이 이 방에서 현재 연결된 플레이어 중에 있나?
  YES -> 에러 NICKNAME_TAKEN
  NO  -> 계속
  |
  v
이 닉네임이 15초 그레이스 중인 끊긴 플레이어와 일치?
  YES -> 재접속 경로:
         - 기존 Player 객체에 새 Connection 바인딩
         - 해당 플레이어의 그레이스 타이머 취소
         - player_reconnected 브로드캐스트
         - joined_room (reconnect=true) + 전체 game_state 스냅샷 송신
         - 종료
  NO  -> 계속
  |
  v
방이 LOCKED 상태 (게임 진행 중)?
  YES -> 에러 ROOM_IN_PROGRESS
  NO  -> 계속
  |
  v
방이 max_players 도달?
  YES -> 에러 ROOM_FULL
  NO  -> 계속
  |
  v
닉네임 형식 검증.
  실패 -> 에러 NICKNAME_INVALID
  성공 -> 계속
  |
  v
신규 입장 경로:
  - Player 객체 생성
  - 방의 플레이어 리스트에 다음 자리 인덱스로 추가
  - joined_room (reconnect=false) + 현재 로비 상태 송신
  - lobby_update 브로드캐스트
  - 종료
```

이 플로우가 사용자가 요구한 핵심 불변식을 강제한다:

1. 끊긴 플레이어의 자리는 오직 같은 닉네임으로만 되찾을 수 있다.
2. 새 닉네임이 그레이스 윈도우 동안 빈 자리에 들어올 수 없다 (게임 시작 후엔 `ROOM_IN_PROGRESS` 에 걸림 — 재접속만 위쪽 분기에서 통과).
3. 닉네임 충돌은 어떤 상태 변경보다 먼저 검사된다.

### 6.5 연결 끊김 처리

TCP 연결이 닫히면 (`recv` 가 0 또는 에러 반환, 또는 송신 실패):

1. 해당 `Player.connection_status = disconnected_at(now)`.
2. 방이 `OPEN` 이면:
   - 그 플레이어가 호스트였으면, 자리 순으로 다음 플레이어를 호스트로 승격. 남은 사람이 없으면 방 `CLOSED`.
   - `lobby_update` 브로드캐스트.
3. 방이 `LOCKED` 이면:
   - 해당 플레이어의 현재 게임 상태를 동결; 그의 차례였어도 진행하지 않음.
   - `player_disconnected` 와 `connected: false` 가 반영된 `game_state` 브로드캐스트.
   - 이벤트 루프가 이 플레이어를 "그레이스 감시 리스트" 에 기록.

이벤트 루프는 매 tick 마다 감시 리스트를 훑어서, `now - disconnected_at > 15s` 인 항목에 타임아웃 처리기를 적용한다:

- `OPEN`: 플레이어를 방에서 제거.
- `LOCKED`:
  - 현재 핸드의 상태를 `folded` 로 표시.
  - `player_left {reason: "timeout"}` 브로드캐스트.
  - BETTING 라운드 종료 조건 재확인 — 이 타임아웃이 정상 종료 또는 "한 명만 남음" fast-path 를 유발했을 수 있음.
  - 새 `game_state` 브로드캐스트.

그레이스 안에 재접속하면 감시 리스트에서 제거되며 타임아웃 처리기는 실행되지 않음.

### 6.6 호스트 역할

- 호스트 = 방을 만든 플레이어.
- 호스트는 오직 `LOBBY` 에서 `start_game` 을 보낼 수 있는 권한만 가짐.
- 호스트가 `LOBBY` 에서 나가면(자발 또는 타임아웃) 자리 순으로 다음 플레이어가 새 호스트가 되고 `lobby_update` 가 브로드캐스트됨.
- `start_game` 이후엔 호스트 역할은 비활성화되어 이후 세션에서 다시 체크되지 않음.

### 6.7 자발적 퇴장

`leave_room` 은 연결 끊김/타임아웃 경로의 즉시 버전으로 처리한다:

- `OPEN`: 해당 플레이어를 방 리스트에서 제거. 호스트였으면 §6.6 규칙에 따라 호스트를 승격. 방이 비면 `CLOSED` 로 전이. `lobby_update` 브로드캐스트.
- `LOCKED`: 현재 핸드에서 `folded` 로 표시한 뒤, 이후 핸드에서는 제거(`eliminated` 에 상당). `player_left {reason: "voluntary"}` 브로드캐스트. BETTING 라운드 종료 체크를 즉시 재실행하며, 이것이 `SHOWDOWN` 또는 fast-path 핸드 종료를 유발할 수 있음.
- 서버가 송신자의 WebSocket 연결을 닫지는 않음; 클라이언트 쪽에서 이어서 닫으면 된다.

### 6.8 방 정리

- `CLOSED` 방: 모든 클라이언트가 마지막 `game_over` 또는 `lobby_update` 를 받은 다음 tick 에 삭제.
- 연결된 플레이어가 0명인 `OPEN` 방: 60초 뒤 삭제.
- 모든 플레이어가 끊긴 `LOCKED` 방: 60초 뒤 삭제.

---

## 7. 보안 및 신뢰 모델

### 7.1 신뢰 모델

이 게임은 이미 다른 채널로 서로 신뢰하는 친구들끼리 하는 소규모 게임이다. 방 코드가 곧 공유 비밀이며, 코드를 아는 사람은 누구나 입장할 수 있다. 계정 시스템도, "네가 주장한 닉네임" 이상의 신원 확인도 없다. 이 프로젝트의 스코프에 의도적으로 적절한 선택이다.

### 7.2 Origin 검증

- WebSocket 핸드셰이크의 `Origin` 헤더를 다음 화이트리스트와 대조한다:
  - `https://<project>.vercel.app` (운영 프런트)
  - `http://localhost:<dev-port>` (로컬 개발)
- 누락 또는 불일치 → HTTP 403 응답 후 TCP 연결 닫음. WebSocket 이 아직 열리지 않았으므로 `ORIGIN_NOT_ALLOWED` JSON 에러가 아닌 HTTP 레벨 에러.

### 7.3 레이트 리밋

- 클라이언트 IP 별로 집계.
- 한도: IP 당 분당 `{create_room | join_room}` 시도 10 회.
- 초과 시: 서버가 `RATE_LIMITED` 에러 메시지를 보내고 연결을 닫음.
- 레이트 리밋은 메모리에만 유지되며 서버 재시작 시 리셋. MVP 에 충분.

### 7.4 닉네임 검증

- 1–16 코드포인트 길이.
- 허용 문자: ASCII 영숫자, 언더스코어, 하이픈, 공백(앞뒤 제외), 한글 음절(가–힣).
- 금지: 제어 문자, zero-width, bidi override 등 모호한 렌더링을 일으키는 것.
- 유효하지 않은 닉네임은 상태 변경 전에 `NICKNAME_INVALID` 응답.

### 7.5 메시지 크기 제한

- 단일 WebSocket 메시지가 4 KB 를 초과하면 close code `1009 Message Too Big` 으로 연결 닫음.
- 정상 게임 메시지는 이 한도 근처에도 못 옴; 이 캡은 과대 프레임을 통한 DoS 를 어렵게 만들기 위한 것.

### 7.6 영속성

- 없음. 모든 상태는 메모리 위에만. 서버 재시작 시 모든 방 소실.
- 세션이 짧고(길어야 수십 분) 보관할 만한 데이터도 없으므로 허용 가능.

---

## 8. 에러 처리

에러는 발생한 레이어에서 처리되며, 비구조적으로 상위로 올리지 않는다.

### 8.1 Layer 1: 프로토콜 에러

- 잘못된 WebSocket 프레임, 잘못된 마스킹, 예약 비트 설정, 알 수 없는 opcode → close code `1002 Protocol Error`.
- 과대 페이로드 → close code `1009 Message Too Big`.
- HTTP Upgrade 핸드셰이크 실패(헤더 누락, 잘못된 버전, 잘못된 Origin) → `HTTP/1.1 400 Bad Request` 또는 `403 Forbidden` 응답 후 TCP 닫음.

### 8.2 Layer 2: 세션 에러

- 알 수 없는 `type`, 필수 필드 누락, JSON 파싱 에러 → JSON `error` 메시지 `MALFORMED_MESSAGE` 로 응답, 연결 유지.
- 입장/방 관련 에러 → 적절한 코드의 `error` 응답, 연결 유지.

### 8.3 Layer 3: 게임 에러

- 차례 아님, 현재 단계에 맞지 않는 액션, 잘못된 레이즈 금액 → 적절한 코드의 `error` 응답. **상태를 전이시키지 않는다.** 연결 유지.

### 8.4 내부 에러

- 핸들러 밖으로 빠져나온 C++ 예외 → 가능하면 스택 컨텍스트와 함께 로그, 해당 연결에 `error {code: "INTERNAL_ERROR"}` 송신, 그 연결만 닫음. 이벤트 루프의 나머지는 계속 동작. **한 연결의 문제가 서버 전체 크래시로 이어지지 않는다.**

---

## 9. 테스트 전략

테스트는 비용이 낮은 것부터 네 개의 층으로 계획한다.

### 9.1 단위 테스트: 프로토콜 파싱 (소켓 없음)

- WebSocket 프레임 디코더를 RFC 6455 § 5.7 의 예제 전부와 추가 엣지 케이스(멀티 프래그먼트, 마스킹, 컨트롤 프레임 interleaved, 페이로드 길이 125/126/127 경계) 로 검증.
- 프레임 인코더는 디코더와 라운드 트립으로 검증.
- HTTP Upgrade 파서를 유효/잘못된 요청(헤더 대소문자, 공백, 누락 변주) 으로 검증.
- SHA-1 + base64 래퍼는 RFC 6455 § 1.3 의 알려진 예제로 검증:
  `dGhlIHNhbXBsZSBub25jZQ==` + magic → `s3pPLMBiTxaQ9kYGzzhZRbK+xOo=`.

### 9.2 단위 테스트: 게임 로직 (소켓 없음)

- `IndianPoker` 상태를 격리된 상태로 직접 생성.
- 결정론적 카드 주입.
- 액션 시퀀스를 넣고 결과 상태와 outgoing 메시지를 assert.
- 커버할 케이스:
  - 정상 2인 핸드 (체크/레이즈/콜/공개).
  - 3인 중 한 명 폴드, 2명 쇼다운.
  - 한 명 빼고 모두 폴드 (SHOWDOWN 스킵 fast path).
  - 동점 분배 + 나머지 처리.
  - 패배 핸드 뒤 탈락.
  - 한 명만 남으면 GAME_OVER.
  - 폴드한 플레이어의 턴 스킵.
  - 게임 중 끊김 → 타임아웃 시 폴드 처리.

### 9.3 통합 테스트: 로컬 호스트 실제 소켓

- 서버를 백그라운드 스레드 또는 서브프로세스로 실행.
- 최소한의 C++ 테스트 WebSocket 클라이언트로 2~4명을 시뮬레이션 (우리가 직접 구현한 인코더/디코더 재사용 가능).
- 시나리오: 방 생성 → 입장 → 시작 → 풀 핸드 → 재접속 → 게임 종료.

### 9.4 수동 테스트

- 개발 머신의 브라우저 탭 2개로 `localhost` 대상.
- 두 기기(노트북 + 폰) 로 Cloudflare Tunnel URL 대상.
- 일부러 WiFi 끊어보며 실제 네트워크 조건에서 재접속 경로 확인.

---

## 10. 배포 아키텍처

### 10.1 토폴로지

```
+-----------------+        HTTPS        +-------------+
|  User browser   | ------------------> | Vercel CDN  |
+-----------------+    (정적 자산)       +-------------+
        |
        | 페이지 로드 후 JS 실행:
        | new WebSocket("wss://game.example.com/")
        |
        v
+------------------+   TLS 종단          +----------------------+
| Cloudflare Edge  | ------------------> | cloudflared (local)  |
+------------------+                     +----------------------+
                                                   |
                                                   | 평문 TCP/WebSocket
                                                   v
                                         +------------------------+
                                         |  C++ 서버 (local)       |
                                         |  127.0.0.1 에 리슨     |
                                         +------------------------+
```

### 10.2 백엔드 배포

- 개발자의 로컬 머신에서 일반 바이너리로 실행.
- 설정(리슨 포트, Origin 허용 목록, 로그 레벨) 은 환경 변수로 읽음.
- `cloudflared` 가 사용자의 Cloudflare 도메인 서브도메인을 `localhost:PORT` 로 터널링하도록 사전 설정.
- MVP 엔 컨테이너, systemd 유닛 없음; 플레이 세션마다 수동 실행.

### 10.3 프런트엔드 배포

- `client/` 디렉토리가 Vercel 에 정적 파일로 배포됨.
- WebSocket URL (`wss://game.example.com/`) 은 JS 상수로 박히거나, 환경별로 바꾸기 쉽도록 작은 `config.js` 에 분리.

### 10.4 CI/CD

MVP 엔 없음. 배포는 Vercel 에 `git push`, 백엔드 수동 실행.

---

## 11. 디렉토리 구조 (예정)

```
indian-poker/
├── client/                   # 정적 프런트, Vercel 로 배포
│   ├── index.html            # 랜딩 페이지: 방 만들기 / 입장
│   ├── css/
│   │   └── style.css
│   └── js/
│       ├── main.js           # 엔트리 포인트; 페이지 라우팅 (로비 vs 게임)
│       ├── ws_client.js      # WebSocket + JSON 디스패치 얇은 래퍼
│       └── ui.js             # game_state -> DOM 렌더
├── server/                   # C++ 서버
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
│   │   ├── igame.h           # 인터페이스만
│   │   └── indian_poker/
│   │       ├── indian_poker.{h,cpp}
│   │       └── state.{h,cpp}
│   └── util/
│       ├── json.{h,cpp}      # nlohmann/json 얇은 래퍼
│       ├── base64.{h,cpp}    # OpenSSL EVP base64 래퍼
│       └── sha1.{h,cpp}      # OpenSSL SHA-1 래퍼
├── tests/
│   ├── test_ws_frame.cpp
│   ├── test_ws_handshake.cpp
│   ├── test_indian_poker.cpp
│   └── test_helpers/
├── docs/
│   └── protocol-examples.md  # 디버깅용 주석 달린 프레임 예제
├── PROJECT.md                # 영문 설계 문서
├── PROJECT.ko.md             # 본 문서
├── README.md
├── README.ko.md
├── LICENSE
└── .gitignore
```

---

## 12. 구현 마일스톤

각 마일스톤은 (작더라도) 실행 가능한 상태로 끝나고 한 번 커밋을 남긴다. 뛰어넘지 않는다. 각 단계가 검증된 뒤에 다음 단계의 복잡도를 추가하기 위해 이 순서로 골랐다.

| # | 마일스톤 | 실행 가능한 결과 |
|:--|:--------|:----------------|
| M1 | `select()` 기반 최소 TCP 에코 서버 | `telnet localhost PORT` 가 에코, 다중 접속 처리 |
| M2 | HTTP Upgrade 핸드셰이크 | 브라우저 `new WebSocket(...)` 이 400 없이 접속 성공 |
| M3 | WebSocket 텍스트 프레임 (프래그멘테이션 X) | 브라우저에서 텍스트 왕복 에코, 마스킹된 클라이언트 프레임 처리 |
| M4 | 프래그멘테이션 + 컨트롤 프레임 (ping/pong/close) | 브라우저의 idle ping/pong 처리, 큰 메시지 프래그먼트 분할 동작 |
| M5 | 세션 레이어 스캐폴딩: Player, Room, RoomRegistry | `create_room` / `join_room` 동작, 로비 업데이트 브로드캐스트 |
| M6 | 로비 흐름 완성, 방 코드 생성, 닉네임 검증 | 두 탭이 같은 방에 들어가 서로 보임 |
| M7 | 연결 끊김 감지 + 15초 재접속 윈도우 | 탭 닫기 → 다른 쪽에 `player_disconnected`; 같은 닉으로 재접속 → 복귀 |
| M8 | `IndianPoker` 게임 로직 (격리 단위 테스트) | `ctest` 가 게임 테스트 슈트 전체를 서버 없이 실행 |
| M9 | 게임 로직을 세션 레이어에 연결: `start_game`, `action`, `game_state` | 두 탭이 풀 핸드 한 판을 끝까지 진행 |
| M10 | `HAND_OVER`, 다음 핸드, 탈락, `GAME_OVER` | 한 게임 세션을 end-to-end 로 진행 |
| M11 | 클라이언트 UI (최소 HTML/CSS/JS) | devtools 가 아니라 브라우저에서 실제로 플레이 가능 |
| M12 | 보안 정리: Origin 검증, 레이트 리밋, 메시지 크기 캡, 닉네임 규칙 | 잘못된 클라이언트 거절이 깔끔 |
| M13 | 배포: 프런트 Vercel, 백엔드 Cloudflare Tunnel | 친구와 폰으로 공용 인터넷에서 첫 실전 플레이테스트 |

---

## 13. 미해결 질문 및 향후 작업

MVP 스코프는 아니지만 기록해 둘 가치가 있는 것들:

- **`IGame` 모듈로 두 번째 게임 붙이기.** 라이어 게임이나 간단한 카드 매칭 게임을 붙여 보면 3-계층 분리가 실제로 동작하는지 검증 가능.
- **관전 모드.** 현재 설계에선 탈락자가 세션에서 빠지는데, 대신 `game_state` 만 받는 관전자로 강등할 수도 있음.
- **턴 타이머.** 현재 설계엔 자기 턴 타이머 없음. 30 초 턴 클럭을 넣으면 스톨링 방지.
- **서버 측 로깅/메트릭.** 메시지 한 줄씩 파일에 남기는 단순 로그만으로도 플레이테스트 중 프로토콜 디버깅에 유용.
- **브라우저 완전 재시작 재접속.** `sessionStorage` 를 넘어선 영속성(`localStorage`) 과 그에 따른 impersonation 위험 수용이 필요.
- **허용 닉네임 정규식 감사.** 유니코드는 토끼 굴; 친구들이 실제로 뭘 입력하는지 본 뒤 재검토.
- **백엔드 Dockerize.** 노트북 + 터널 대신 소형 VPS 에 배포하고 싶을 때 단순해짐.
- **C++ 서버 내 TLS.** Cloudflare 의 TLS 종단을 프로세스 내 TLS(OpenSSL 또는 mbedTLS) 로 대체하는 건, 프로젝트가 이어진다면 논리적인 다음 단계.

---

*디자인 문서 끝.*
