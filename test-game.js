// End-to-end test: simulate 2/3/4-player games via WebSocket
const WebSocket = require('ws');

const WS_URL = 'ws://localhost:8080/';
const ORIGIN = 'http://localhost:3000';

function createPlayer(nick) {
    return new Promise((resolve, reject) => {
        const ws = new WebSocket(WS_URL, { origin: ORIGIN });
        const msgs = [];
        const waiters = [];
        const player = {
            nick,
            ws,
            msgs,
            send: (obj) => ws.send(JSON.stringify(obj)),
            waitFor: (type, timeout = 3000) => new Promise((res, rej) => {
                // Check existing messages
                const existing = msgs.find(m => m.type === type);
                if (existing) return res(existing);
                const timer = setTimeout(() => rej(new Error(`timeout waiting for ${type} (${nick})`)), timeout);
                waiters.push({ type, resolve: (m) => { clearTimeout(timer); res(m); } });
            }),
            close: () => ws.close(),
        };
        ws.on('open', () => resolve(player));
        ws.on('error', reject);
        ws.on('message', (data) => {
            const m = JSON.parse(data);
            msgs.push(m);
            for (let i = waiters.length - 1; i >= 0; i--) {
                if (waiters[i].type === m.type) {
                    waiters[i].resolve(m);
                    waiters.splice(i, 1);
                }
            }
        });
    });
}

async function testGame(numPlayers) {
    console.log(`\n═══ ${numPlayers}인 게임 테스트 ═══`);
    const nicks = ['alice', 'bob', 'carol', 'dave'].slice(0, numPlayers);
    const players = [];
    for (const n of nicks) players.push(await createPlayer(n));

    // Host creates room
    players[0].send({ type: 'create_room', nickname: nicks[0], max_players: numPlayers });
    const created = await players[0].waitFor('room_created');
    const code = created.room_code;
    console.log(`  방 생성: ${code}, 호스트: ${nicks[0]}`);

    // Others join
    for (let i = 1; i < numPlayers; i++) {
        players[i].send({ type: 'join_room', room_code: code, nickname: nicks[i] });
        await players[i].waitFor('joined_room');
        console.log(`  ${nicks[i]} 참가`);
    }

    // Wait for all to see the full lobby
    await new Promise(r => setTimeout(r, 100));

    // Host starts
    players[0].send({ type: 'start_game' });
    for (const p of players) await p.waitFor('game_state');
    const state = players[0].msgs.filter(m => m.type === 'game_state').pop();
    console.log(`  게임 시작: phase=${state.phase}, 팟=${state.pot}, 칩당=${state.players[0].chips}`);

    if (state.phase !== 'BETTING') throw new Error(`Expected BETTING, got ${state.phase}`);
    if (state.pot !== 5 * numPlayers) throw new Error(`Expected pot ${5*numPlayers}, got ${state.pot}`);

    // Verify card visibility: each player sees others' cards but not their own
    for (const p of players) {
        const s = p.msgs.filter(m => m.type === 'game_state').pop();
        const me = s.players.find(pp => pp.nickname === p.nick);
        const others = s.players.filter(pp => pp.nickname !== p.nick);
        if (me.card !== null) throw new Error(`${p.nick} sees own card!`);
        for (const o of others) {
            if (o.card === null || o.card < 1 || o.card > 10) throw new Error(`${p.nick} doesn't see ${o.nickname}'s card`);
        }
    }
    console.log('  ✓ 카드 가시성 OK (본인 card=null, 상대 card=1~10)');

    // Play a hand: everyone except one folds (first turn player folds initial round robin)
    // Simple flow: all except the last active player fold
    let handOverReached = false;
    let foldCount = 0;
    const maxSteps = numPlayers * 3;
    for (let step = 0; step < maxSteps && !handOverReached; step++) {
        const last = players[0].msgs.filter(m => m.type === 'game_state').pop();
        if (last.phase === 'HAND_OVER') { handOverReached = true; break; }
        if (last.phase !== 'BETTING') break;
        const turnNick = last.current_turn;
        const turnPlayer = players.find(p => p.nick === turnNick);
        // Fold everyone except the last player
        const activePlayers = last.players.filter(p => p.status === 'active').length;
        if (activePlayers > 1) {
            turnPlayer.send({ type: 'action', action: 'fold' });
            foldCount++;
        }
        // Wait for updated state
        await new Promise(r => setTimeout(r, 50));
    }

    const handOver = players[0].msgs.find(m => m.type === 'hand_over');
    if (!handOver) throw new Error('hand_over 이벤트 못받음');
    console.log(`  ✓ 핸드 종료: ${handOver.winner} 승리 +${handOver.pot_won}, ${foldCount}명 폴드`);

    // Ready next hand from all
    for (const p of players) p.send({ type: 'ready_next_hand' });
    await new Promise(r => setTimeout(r, 200));

    const afterReady = players[0].msgs.filter(m => m.type === 'game_state').pop();
    if (afterReady.hand_number !== 2) throw new Error(`Expected hand 2, got ${afterReady.hand_number}`);
    if (afterReady.phase !== 'BETTING') throw new Error(`Expected BETTING, got ${afterReady.phase}`);
    console.log(`  ✓ 다음 핸드: hand=${afterReady.hand_number}, phase=${afterReady.phase}`);

    // Test showdown: everyone checks
    let showdownReached = false;
    for (let step = 0; step < numPlayers * 2 && !showdownReached; step++) {
        const last = players[0].msgs.filter(m => m.type === 'game_state').pop();
        if (last.phase === 'HAND_OVER') { showdownReached = true; break; }
        if (last.phase !== 'BETTING') break;
        const turnNick = last.current_turn;
        const turnPlayer = players.find(p => p.nick === turnNick);
        turnPlayer.send({ type: 'action', action: 'check' });
        await new Promise(r => setTimeout(r, 50));
    }
    const showdownHandOver = players[0].msgs.filter(m => m.type === 'hand_over').pop();
    if (!showdownReached || !showdownHandOver) throw new Error('쇼다운까지 진행 안 됨');
    console.log(`  ✓ 쇼다운: ${showdownHandOver.winner} 승리 +${showdownHandOver.pot_won}, 공개=${JSON.stringify(showdownHandOver.revealed)}`);

    for (const p of players) p.close();
    console.log(`  ✅ ${numPlayers}인 게임 정상 작동`);
}

async function testRaise() {
    console.log('\n═══ 레이즈 액션 테스트 ═══');
    const players = [await createPlayer('alice'), await createPlayer('bob')];

    players[0].send({ type: 'create_room', nickname: 'alice', max_players: 2 });
    const code = (await players[0].waitFor('room_created')).room_code;
    players[1].send({ type: 'join_room', room_code: code, nickname: 'bob' });
    await players[1].waitFor('joined_room');
    await new Promise(r => setTimeout(r, 100));
    players[0].send({ type: 'start_game' });
    for (const p of players) await p.waitFor('game_state');

    let s = players[0].msgs.filter(m => m.type === 'game_state').pop();
    const starter = s.current_turn;
    const starterPlayer = players.find(p => p.nick === starter);
    const otherPlayer = players.find(p => p.nick !== starter);

    console.log(`  시작: pot=${s.pot}, current_bet=${s.current_bet}, turn=${starter}`);

    // Test 1: Raise below min_raise (should error)
    starterPlayer.msgs.length = 0;
    starterPlayer.send({ type: 'action', action: 'raise', amount: 7 });
    await new Promise(r => setTimeout(r, 100));
    const err = starterPlayer.msgs.find(m => m.type === 'error');
    if (!err || err.code !== 'INVALID_RAISE_AMOUNT') throw new Error(`Expected INVALID_RAISE_AMOUNT, got ${err ? err.code : 'none'}`);
    console.log(`  ✓ 최소 미만 레이즈 거절 (${err.code})`);

    // Test 2: Valid raise to 15 (current_bet 5 + min_raise 10)
    starterPlayer.send({ type: 'action', action: 'raise', amount: 15 });
    await new Promise(r => setTimeout(r, 100));
    s = players[0].msgs.filter(m => m.type === 'game_state').pop();
    if (s.current_bet !== 15) throw new Error(`Expected current_bet 15, got ${s.current_bet}`);
    if (s.pot !== 20) throw new Error(`Expected pot 20, got ${s.pot}`);
    if (s.current_turn === starter) throw new Error(`Turn should have advanced`);
    const raiserState = s.players.find(p => p.nickname === starter);
    if (raiserState.bet_this_round !== 15) throw new Error(`Raiser bet_this_round should be 15`);
    if (raiserState.chips !== 85) throw new Error(`Raiser chips should be 85, got ${raiserState.chips}`);
    console.log(`  ✓ 레이즈 15 적용: pot=${s.pot}, current_bet=${s.current_bet}, 칩=${raiserState.chips}`);

    // Test 3: Other calls (raises to match) — needs to raise to at least 15+10=25, or just match via raise
    // Actually the game doesn't have "call" — to match we use the same raise action at the current_bet.
    // But our raise validation requires amount >= current_bet + min_raise OR all-in.
    // So bob can only "raise" higher. Let's raise to 25 then alice needs to call.
    otherPlayer.send({ type: 'action', action: 'raise', amount: 25 });
    await new Promise(r => setTimeout(r, 100));
    s = players[0].msgs.filter(m => m.type === 'game_state').pop();
    if (s.current_bet !== 25) throw new Error(`Expected current_bet 25`);
    console.log(`  ✓ 재레이즈 25 적용: pot=${s.pot}, current_bet=${s.current_bet}`);

    // starter needs to match 25 — but "check" would fail since bet_this_round(15) != current_bet(25)
    starterPlayer.msgs.length = 0;
    starterPlayer.send({ type: 'action', action: 'check' });
    await new Promise(r => setTimeout(r, 100));
    const checkErr = starterPlayer.msgs.find(m => m.type === 'error');
    if (!checkErr) throw new Error('Check with unmatched bet should fail');
    console.log(`  ✓ 베팅 불일치 시 체크 거절 (${checkErr.code})`);

    // Fold to end the hand
    starterPlayer.send({ type: 'action', action: 'fold' });
    await new Promise(r => setTimeout(r, 200));
    const ho = players[0].msgs.find(m => m.type === 'hand_over');
    if (!ho || ho.winner !== otherPlayer.nick) throw new Error(`Expected ${otherPlayer.nick} to win, got ${ho?.winner}`);
    console.log(`  ✓ 핸드 종료: ${ho.winner} 승리 +${ho.pot_won}`);

    for (const p of players) p.close();
    console.log(`  ✅ 레이즈 액션 정상 작동`);
}

(async () => {
    try {
        await testGame(2);
        await testGame(3);
        await testGame(4);
        await testRaise();
        console.log('\n━━━━━━━━━━━━━━━━━━━━━━━━');
        console.log('✅ 2/3/4인 게임 + 레이즈 전부 통과');
        process.exit(0);
    } catch (e) {
        console.error('\n❌ 실패:', e.message);
        console.error(e.stack);
        process.exit(1);
    }
})();
