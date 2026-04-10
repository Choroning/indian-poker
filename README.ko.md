# 인디언포커 — 통신 기반 미니게임

![Last Commit](https://img.shields.io/github/last-commit/Choroning/indian-poker)
![Languages](https://img.shields.io/github/languages/top/Choroning/indian-poker)
![Status](https://img.shields.io/badge/status-design-yellow)
![License](https://img.shields.io/badge/license-MIT-blue)

컴퓨터네트워크, 객체지향프로그래밍및실습, 운영체제 수업에서 배운 개념을 실제 동작하는 프로젝트로 엮어 만드는, 바닥부터 직접 짠 실시간 멀티플레이어 인디언포커 게임입니다.

한 명이 방을 만들면 나머지는 6자리 방 코드를 입력해 입장하고, 여러 기기의 브라우저에서 함께 플레이할 수 있습니다.

*작성자: 박철원 (고려대학교(세종), 컴퓨터소프트웨어학과) - 2026년 기준 3학년*
<br><br>

## 📑 목차

- [프로젝트 소개](#about-this-project)
- [학습 동기](#learning-motivation)
- [게임 규칙](#game-rules)
- [기술 스택](#technology-stack)
- [아키텍처 개요](#architecture-overview)
- [디자인 문서](#design-document)
- [프로젝트 상태](#project-status)
- [라이선스](#license)

---

<br><a name="about-this-project"></a>
## 📝 프로젝트 소개

인디언포커는 각 플레이어가 카드 한 장을 받아 자기 이마에 대고 — 남의 카드는 보이지만 자기 카드는 보이지 않는 — 상태에서 베팅하는 간단한 카드 게임입니다. 각자 보이는 남의 카드를 근거로 자기 카드가 높을지 낮을지 추론해 베팅하고, 끝까지 남은 플레이어 중 가장 높은 카드를 가진 사람이 팟을 가져갑니다.

이 레포는 그 게임을 브라우저 기반 멀티플레이어 미니게임으로 구현합니다. 단, 한 가지 중요한 차이가 있습니다: **서버를 C++로 raw TCP 소켓 위에서 처음부터 직접 작성하고, WebSocket 프로토콜 역시 라이브러리 없이 직접 구현합니다.** 목표는 단순히 "동작하는 게임 하나 완성" 이 아니라, 실시간 클라이언트-서버 통신이 프로토콜 레벨에서 실제로 어떻게 동작하는지를 몸으로 이해하는 것입니다.

아키텍처는 "재사용 가능한 방/세션/네트워크 인프라" 와 "게임별 로직" 을 엄격히 분리하여, 네트워크 레이어를 건드리지 않고도 새로운 미니게임을 모듈로 추가할 수 있도록 설계되었습니다.

<br><a name="learning-motivation"></a>
## 🎓 학습 동기

이 프로젝트는 고려대학교(세종)의 학부 수업 세 과목의 교차점에 위치합니다:

| 과목 | 학기 | 적용 개념 |
|:-----|:----|:---------|
| **컴퓨터네트워크** (`DCCS307-01`) | 2025학년도 봄학기 | TCP 소켓 프로그래밍, 프로토콜 설계, HTTP 메시지 파싱, 전송 계층 위에 얹는 응용 계층 프로토콜 |
| **객체지향프로그래밍및실습** (`DCCS213-01`) | 2025학년도 봄학기 | 클라이언트-서버 아키텍처 (Java `ServerSocket` 으로 이미 연습), 관심사 분리, 미니게임 모듈을 플러그인 형태로 꽂기 위한 인터페이스 기반 다형성 |
| **운영체제** (`DCCS309-01`) | 2026학년도 봄학기 (현재 수강 중) | `select(2)` 를 이용한 I/O 멀티플렉싱, 단일 스레드 이벤트 기반 동시성, 파일 디스크립터 관리, 락 없는 경쟁 조건 회피 |

컴퓨터네트워크 텀프로젝트(Python Simple HTTP Server)와 OOP 텀프로젝트(Java Multiplayer Chat Application)가 이 프로젝트의 직접적인 디딤돌 역할을 합니다. 이 프로젝트는 두 방향에서 동시에 한 걸음씩 더 나아갑니다:

1. **더 깊은 프로토콜 작업** — HTTP는 이미 익숙한, 줄 단위 텍스트 프로토콜이었습니다. 반면 WebSocket(RFC 6455)은 바이너리 프레이밍, 마스킹, 오프코드, 프래그멘테이션, 컨트롤 프레임 등을 포함하며, 이 모두를 바이트 단위로 직접 파싱하고 생성해야 합니다.
2. **다른 동시성 모델** — Java 채팅 서버는 connection-per-thread 구조였습니다. 이 프로젝트는 현재 운영체제 수업에서 다루고 있는 I/O 멀티플렉싱 개념을 반영해, `select()` 기반 단일 스레드 이벤트 루프를 채택합니다.

<br><a name="game-rules"></a>
## 🎴 게임 규칙

- **방 정원:** 2명 ~ 4명 (방 생성 시 방장이 결정).
- **덱:** 1부터 10까지의 숫자 카드, 매 핸드마다 새로 셔플.
- **은닉 정보:** 각 플레이어는 자기 카드를 볼 수 없고 남들의 카드만 보임.
- **한 핸드의 흐름:**
  1. 남아 있는 모든 플레이어가 앤티를 팟에 냅니다.
  2. 각자 카드 1장을 받습니다.
  3. 베팅 한 라운드가 시계방향으로 진행됩니다. 자기 차례에 **Check**(현재 베팅에 맞추기), **Raise**(베팅 올리기), **Fold**(기권) 중 하나를 선택합니다.
  4. 남은 모든 플레이어가 최고 베팅에 맞추거나, 한 명만 남으면 베팅 라운드가 종료됩니다.
  5. 남은 플레이어들의 카드를 공개하고, 가장 높은 카드의 주인이 팟을 가져갑니다.
- **탈락 조건:** 칩이 0이 된 플레이어는 탈락. 마지막 1명이 남으면 게임 종료.

<br><a name="technology-stack"></a>
## 🛠 기술 스택

**서버 (직접 작성):**
- 언어: **C++** (C++17 이상)
- 네트워킹: POSIX BSD 소켓 — `socket`, `bind`, `listen`, `accept`, `recv`, `send`
- 동시성: **`select(2)`** 기반 단일 스레드 이벤트 루프
- WebSocket: **RFC 6455 직접 구현** — HTTP Upgrade 핸드셰이크, 프레임 파싱/생성, 마스킹, 프래그멘테이션, 핑/퐁, 클로즈 프레임
- 외부 의존성: **OpenSSL** (SHA-1 + base64, WebSocket 핸드셰이크에만 사용), 메시지 직렬화용 JSON 라이브러리

**클라이언트:**
- 정적 HTML/CSS + **바닐라 JavaScript** — React/Vue/Svelte 없음, 빌드 단계 없음
- 브라우저 내장 `WebSocket` API
- `sessionStorage` 는 실수로 새로고침했을 때 현재 방 코드를 유지하는 용도로만 최소 사용

**배포:**
- **프런트엔드 (정적)**: Vercel — 사용자가 처음 접속하는 랜딩 페이지(방 만들기 / 입장 화면)와 게임 UI 를 서빙
- **백엔드 (게임 서버)**: 로컬 머신에서 실행하고, **Cloudflare Tunnel** (`cloudflared`)로 공개. TLS 는 Cloudflare 엣지에서 종단되므로 C++ 서버는 평문 WebSocket 만 다룸
- 브라우저는 Vercel 에서 페이지를 로드한 뒤, Cloudflare 터널로 공개된 백엔드에 `wss://` 로 접속

<br><a name="architecture-overview"></a>
## 🏛 아키텍처 개요

서버는 세 개의 명확한 계층으로 나뉘며, 각 계층은 오직 아래 계층의 API에만 의존합니다:

```plaintext
+------------------------------------------------------------+
|  Layer 3: Game Logic                                       |
|    - IGame 인터페이스 (start, handle_action, snapshot_for) |
|    - IndianPoker : IGame                                   |
+------------------------------------------------------------+
|  Layer 2: Session / Room Infrastructure                    |
|    - Player (닉네임, 칩, 연결 상태)                        |
|    - Room (방 코드, 플레이어 리스트, 게임 인스턴스)        |
|    - RoomRegistry (방 생성/검색/정리)                      |
|    - Router (어떤 플레이어에게 어떤 메시지를 보낼지)       |
+------------------------------------------------------------+
|  Layer 1: Network / Protocol                               |
|    - TCPListener, Connection                               |
|    - WebSocketHandshake (HTTP Upgrade)                     |
|    - WebSocketFrame (디코드, 인코드, 마스킹, 프래그먼트)   |
|    - EventLoop (select fd_set 관리)                        |
+------------------------------------------------------------+
```

핵심 설계 원칙:

- **Layer 1 은 게임을 모른다.** 완성된 텍스트 메시지 한 건만 Layer 2 로 올려 보냄.
- **Layer 2 는 게임 규칙을 모른다.** `IGame*` 포인터 하나만 들고 있고, 내부 상태를 들여다보지 않음.
- **Layer 3 는 네트워크를 모른다.** 게임 로직은 순수한 상태 머신: `handle_action(state, player, action) → new_state + outgoing messages`. 덕분에 게임 로직만 따로 단위 테스트가 가능 (소켓 없이).

전체 디자인 문서 — 애플리케이션 프로토콜, 상태 머신, 에러 처리, 테스트 전략 등 — 는 [`PROJECT.md`](PROJECT.md) (영문) / [`PROJECT.ko.md`](PROJECT.ko.md) (한국어) 에 있습니다.

<br><a name="design-document"></a>
## 📐 디자인 문서

전체 디자인 — 애플리케이션 레이어 프로토콜, 게임 상태 머신, 방 수명 관리, 연결 끊김/재접속 시멘틱, 에러 처리, 테스트 전략 — 은 [`PROJECT.md`](PROJECT.md) (영문) 와 [`PROJECT.ko.md`](PROJECT.ko.md) (한국어) 에 담겨 있습니다.

<br><a name="project-status"></a>
## 🚧 프로젝트 상태

현재 **설계 단계** 입니다. 디자인 문서는 완성되었고, 구현은 아직 시작 전입니다. 레포지토리 구조, 빌드 방법, 실행 방법은 구현 진행에 따라 추가될 예정입니다.

<br><a name="license"></a>
## 🤝 라이선스

이 레포지토리는 [MIT License](LICENSE) 하에 배포됩니다.

---
