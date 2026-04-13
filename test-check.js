// Focused test for the `check` action across game states.
const WebSocket = require('ws');

function createPlayer(nick) {
    return new Promise((resolve, reject) => {
        const ws = new WebSocket('ws://localhost:8080/', { origin: 'http://localhost:3000' });
        const msgs = [];
        const p = {
            nick, ws, msgs,
            send: (o) => ws.send(JSON.stringify(o)),
            waitFor: (t, timeout = 2000) => new Promise((res, rej) => {
                const found = msgs.find(m => m.type === t);
                if (found) return res(found);
                const poll = setInterval(() => {
                    const m = msgs.find(mm => mm.type === t);
                    if (m) { clearInterval(poll); clearTimeout(to); res(m); }
                }, 20);
                const to = setTimeout(() => { clearInterval(poll); rej(new Error(`timeout ${t} ${nick}`)); }, timeout);
            }),
            close: () => ws.close(),
        };
        ws.on('open', () => resolve(p));
        ws.on('error', reject);
        ws.on('message', (d) => msgs.push(JSON.parse(d.toString())));
    });
}

async function test_checkAtHandStart() {
    console.log('\n═══ 핸드 시작 직후 체크 ═══');
    const a = await createPlayer('alice'), b = await createPlayer('bob');
    a.send({ type: 'create_room', nickname: 'alice', max_players: 2 });
    const code = (await a.waitFor('room_created')).room_code;
    b.send({ type: 'join_room', room_code: code, nickname: 'bob' });
    await b.waitFor('joined_room');
    await new Promise(r => setTimeout(r, 80));
    a.send({ type: 'start_game' });
    for (const p of [a, b]) await p.waitFor('game_state');

    let s = a.msgs.filter(m => m.type === 'game_state').pop();
    const turn1 = s.current_turn;
    const turnP = [a, b].find(p => p.nick === turn1);
    const otherP = [a, b].find(p => p.nick !== turn1);

    console.log(`  초기: pot=${s.pot}, current_bet=${s.current_bet}, turn=${turn1}`);

    // 1st player checks - should succeed and advance turn
    turnP.msgs.length = 0;
    turnP.send({ type: 'action', action: 'check' });
    await new Promise(r => setTimeout(r, 100));
    const err1 = turnP.msgs.find(m => m.type === 'error');
    if (err1) throw new Error(`Check 1 failed: ${err1.code} / ${err1.message}`);
    s = a.msgs.filter(m => m.type === 'game_state').pop();
    if (s.current_turn === turn1) throw new Error('Turn should have advanced');
    console.log(`  ✓ ${turn1} 체크 성공 → 턴 이동 (${s.current_turn})`);

    // 2nd player checks - all matched, should trigger showdown
    otherP.send({ type: 'action', action: 'check' });
    await new Promise(r => setTimeout(r, 150));
    const handOver = a.msgs.find(m => m.type === 'hand_over');
    if (!handOver) throw new Error('Expected hand_over after all check');
    console.log(`  ✓ ${otherP.nick} 체크 → 쇼다운: ${handOver.winner} 승리`);

    a.close(); b.close();
}

async function test_checkAfterRaise() {
    console.log('\n═══ 레이즈 후 체크 시도 (거절 예상) ═══');
    const a = await createPlayer('alice'), b = await createPlayer('bob');
    a.send({ type: 'create_room', nickname: 'alice', max_players: 2 });
    const code = (await a.waitFor('room_created')).room_code;
    b.send({ type: 'join_room', room_code: code, nickname: 'bob' });
    await b.waitFor('joined_room');
    await new Promise(r => setTimeout(r, 80));
    a.send({ type: 'start_game' });
    for (const p of [a, b]) await p.waitFor('game_state');

    let s = a.msgs.filter(m => m.type === 'game_state').pop();
    const starter = [a, b].find(p => p.nick === s.current_turn);
    const other = [a, b].find(p => p.nick !== s.current_turn);

    starter.send({ type: 'action', action: 'raise', amount: 15 });
    await new Promise(r => setTimeout(r, 80));
    s = a.msgs.filter(m => m.type === 'game_state').pop();
    if (s.current_bet !== 15) throw new Error(`Expected current_bet 15`);
    console.log(`  ${starter.nick} 레이즈 15 → current_bet=15`);

    // Other tries to check (should fail - their bet_this_round=5 < current_bet=15)
    other.msgs.length = 0;
    other.send({ type: 'action', action: 'check' });
    await new Promise(r => setTimeout(r, 100));
    const err = other.msgs.find(m => m.type === 'error');
    if (!err || err.code !== 'INVALID_ACTION') throw new Error(`Expected INVALID_ACTION, got ${err?.code}`);
    console.log(`  ✓ ${other.nick} 체크 거절 (${err.code}: ${err.message})`);

    // State should be unchanged
    const s2 = a.msgs.filter(m => m.type === 'game_state').pop();
    if (s2.current_turn !== other.nick) throw new Error(`Turn should still be on ${other.nick}`);
    if (s2.current_bet !== 15) throw new Error(`current_bet should still be 15`);
    console.log(`  ✓ 상태 불변: turn=${s2.current_turn}, current_bet=${s2.current_bet}`);

    a.close(); b.close();
}

async function test_checkAfterCall() {
    console.log('\n═══ 콜로 매치 후 다음 핸드에서 체크 ═══');
    const a = await createPlayer('alice'), b = await createPlayer('bob');
    a.send({ type: 'create_room', nickname: 'alice', max_players: 2 });
    const code = (await a.waitFor('room_created')).room_code;
    b.send({ type: 'join_room', room_code: code, nickname: 'bob' });
    await b.waitFor('joined_room');
    await new Promise(r => setTimeout(r, 80));
    a.send({ type: 'start_game' });
    for (const p of [a, b]) await p.waitFor('game_state');

    // Hand 1: starter raises, other calls → showdown → next hand
    let s = a.msgs.filter(m => m.type === 'game_state').pop();
    const starter = [a, b].find(p => p.nick === s.current_turn);
    const other = [a, b].find(p => p.nick !== s.current_turn);
    starter.send({ type: 'action', action: 'raise', amount: 15 });
    await new Promise(r => setTimeout(r, 80));
    other.send({ type: 'action', action: 'call' });
    await new Promise(r => setTimeout(r, 200));
    a.send({ type: 'ready_next_hand' });
    b.send({ type: 'ready_next_hand' });
    await new Promise(r => setTimeout(r, 200));

    s = a.msgs.filter(m => m.type === 'game_state').pop();
    if (s.phase !== 'BETTING') throw new Error(`Expected BETTING in hand 2, got ${s.phase}`);
    if (s.hand_number !== 2) throw new Error(`Expected hand 2, got ${s.hand_number}`);
    console.log(`  핸드 2 시작: pot=${s.pot}, current_bet=${s.current_bet}, turn=${s.current_turn}`);

    // Both check → showdown
    const turn2 = [a, b].find(p => p.nick === s.current_turn);
    const other2 = [a, b].find(p => p.nick !== s.current_turn);
    turn2.msgs.length = 0;
    turn2.send({ type: 'action', action: 'check' });
    await new Promise(r => setTimeout(r, 80));
    if (turn2.msgs.find(m => m.type === 'error')) throw new Error('first check failed');
    other2.send({ type: 'action', action: 'check' });
    await new Promise(r => setTimeout(r, 150));
    const ho = a.msgs.filter(m => m.type === 'hand_over').pop();
    if (!ho || ho.hand_number !== 2) throw new Error('hand_over not for hand 2');
    console.log(`  ✓ 핸드 2: 전원 체크 → 쇼다운 ${ho.winner} 승리`);

    a.close(); b.close();
}

async function test_threePlayerCheckChain() {
    console.log('\n═══ 3인 전원 체크 → 쇼다운 ═══');
    const ps = await Promise.all(['alice','bob','carol'].map(n => createPlayer(n)));
    ps[0].send({ type: 'create_room', nickname: 'alice', max_players: 3 });
    const code = (await ps[0].waitFor('room_created')).room_code;
    for (let i=1; i<3; i++) ps[i].send({ type: 'join_room', room_code: code, nickname: ps[i].nick });
    for (let i=1; i<3; i++) await ps[i].waitFor('joined_room');
    await new Promise(r => setTimeout(r, 80));
    ps[0].send({ type: 'start_game' });
    for (const p of ps) await p.waitFor('game_state');

    // Each player checks in turn
    for (let round = 0; round < 3; round++) {
        const s = ps[0].msgs.filter(m => m.type === 'game_state').pop();
        if (s.phase === 'HAND_OVER') break;
        const turnP = ps.find(p => p.nick === s.current_turn);
        turnP.msgs.length = 0;
        turnP.send({ type: 'action', action: 'check' });
        await new Promise(r => setTimeout(r, 100));
        const err = turnP.msgs.find(m => m.type === 'error');
        if (err) throw new Error(`${turnP.nick} check failed: ${err.code}`);
        console.log(`    ${turnP.nick} 체크`);
    }
    const ho = ps[0].msgs.find(m => m.type === 'hand_over');
    if (!ho) throw new Error('hand_over not received');
    console.log(`  ✓ 3인 전원 체크 → ${ho.winner} 승리 +${ho.pot_won}`);

    for (const p of ps) p.close();
}

(async () => {
    try {
        await test_checkAtHandStart();
        await test_checkAfterRaise();
        await test_checkAfterCall();
        await test_threePlayerCheckChain();
        console.log('\n━━━━━━━━━━━━━━━━━━━━━━━━');
        console.log('✅ 체크 기능 전부 정상 작동');
        process.exit(0);
    } catch (e) {
        console.error('\n❌ 실패:', e.message);
        process.exit(1);
    }
})();
