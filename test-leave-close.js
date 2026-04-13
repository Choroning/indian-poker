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

async function test1_leaveBeforeStart() {
    console.log('\n═══ 시작 전 참가자 나가기 ═══');
    const a = await createPlayer('alice'), b = await createPlayer('bob'), c = await createPlayer('carol');
    a.send({ type: 'create_room', nickname: 'alice', max_players: 3 });
    const code = (await a.waitFor('room_created')).room_code;
    b.send({ type: 'join_room', room_code: code, nickname: 'bob' });
    c.send({ type: 'join_room', room_code: code, nickname: 'carol' });
    await b.waitFor('joined_room'); await c.waitFor('joined_room');
    await new Promise(r => setTimeout(r, 80));

    // bob leaves
    b.send({ type: 'leave_room' });
    await b.waitFor('you_left');
    console.log('  ✓ bob: you_left 수신');

    // alice and carol should see player_left + lobby_update
    await new Promise(r => setTimeout(r, 100));
    const aleft = a.msgs.find(m => m.type === 'player_left');
    if (!aleft || aleft.nickname !== 'bob') throw new Error('alice not notified');
    const aLobby = a.msgs.filter(m => m.type === 'lobby_update').pop();
    if (aLobby.players.length !== 2) throw new Error(`Expected 2 players, got ${aLobby.players.length}`);
    console.log(`  ✓ 다른 참가자에게 알림 (${aLobby.players.join(', ')} 남음)`);

    a.close(); b.close(); c.close();
}

async function test2_closeRoomByHost() {
    console.log('\n═══ 호스트 방 닫기 ═══');
    const a = await createPlayer('alice'), b = await createPlayer('bob');
    a.send({ type: 'create_room', nickname: 'alice', max_players: 2 });
    const code = (await a.waitFor('room_created')).room_code;
    b.send({ type: 'join_room', room_code: code, nickname: 'bob' });
    await b.waitFor('joined_room');
    await new Promise(r => setTimeout(r, 80));

    a.send({ type: 'close_room' });
    const aClosed = await a.waitFor('room_closed');
    const bClosed = await b.waitFor('room_closed');
    if (aClosed.reason !== 'host_closed') throw new Error(`Expected host_closed, got ${aClosed.reason}`);
    if (bClosed.reason !== 'host_closed') throw new Error(`bob reason wrong`);
    console.log('  ✓ 양쪽 모두 room_closed 수신 (reason: host_closed)');

    a.close(); b.close();
}

async function test3_nonHostCantClose() {
    console.log('\n═══ 비호스트 close_room 거절 ═══');
    const a = await createPlayer('alice'), b = await createPlayer('bob');
    a.send({ type: 'create_room', nickname: 'alice', max_players: 2 });
    const code = (await a.waitFor('room_created')).room_code;
    b.send({ type: 'join_room', room_code: code, nickname: 'bob' });
    await b.waitFor('joined_room');
    await new Promise(r => setTimeout(r, 80));

    b.msgs.length = 0;
    b.send({ type: 'close_room' });
    await new Promise(r => setTimeout(r, 100));
    const err = b.msgs.find(m => m.type === 'error');
    if (!err || err.code !== 'NOT_HOST') throw new Error(`Expected NOT_HOST, got ${err?.code}`);
    console.log(`  ✓ 비호스트 차단 (${err.code})`);

    a.close(); b.close();
}

async function test4_hostLeaveClosesRoom() {
    console.log('\n═══ 호스트 나가면 방 자동 종료 ═══');
    const a = await createPlayer('alice'), b = await createPlayer('bob');
    a.send({ type: 'create_room', nickname: 'alice', max_players: 2 });
    const code = (await a.waitFor('room_created')).room_code;
    b.send({ type: 'join_room', room_code: code, nickname: 'bob' });
    await b.waitFor('joined_room');
    await new Promise(r => setTimeout(r, 80));

    a.send({ type: 'leave_room' });
    const bClosed = await b.waitFor('room_closed');
    if (bClosed.reason !== 'host_left') throw new Error(`Expected host_left, got ${bClosed.reason}`);
    console.log(`  ✓ bob에게 room_closed (reason: ${bClosed.reason})`);

    a.close(); b.close();
}

async function test5_leaveDuringGame() {
    console.log('\n═══ 게임 중 나가기 ═══');
    const a = await createPlayer('alice'), b = await createPlayer('bob'), c = await createPlayer('carol');
    a.send({ type: 'create_room', nickname: 'alice', max_players: 3 });
    const code = (await a.waitFor('room_created')).room_code;
    b.send({ type: 'join_room', room_code: code, nickname: 'bob' });
    c.send({ type: 'join_room', room_code: code, nickname: 'carol' });
    await b.waitFor('joined_room'); await c.waitFor('joined_room');
    await new Promise(r => setTimeout(r, 80));

    a.send({ type: 'start_game' });
    await Promise.all([a, b, c].map(p => p.waitFor('game_state')));

    // carol leaves mid-game
    c.send({ type: 'leave_room' });
    await c.waitFor('you_left');
    await new Promise(r => setTimeout(r, 100));
    const aleft = a.msgs.find(m => m.type === 'player_left');
    if (!aleft || aleft.nickname !== 'carol') throw new Error('leave notify fail');
    console.log(`  ✓ carol 퇴장, 나머지에 알림`);
    // Game state shows all players but carol is eliminated
    const aState = a.msgs.filter(m => m.type === 'game_state').pop();
    const carolState = aState.players.find(p => p.nickname === 'carol');
    if (!carolState || carolState.status !== 'eliminated') throw new Error(`carol should be eliminated, got ${carolState?.status}`);
    const active = aState.players.filter(p => p.status !== 'eliminated');
    if (active.length !== 2) throw new Error(`Expected 2 active, got ${active.length}`);
    console.log(`  ✓ 게임 계속 (carol=eliminated, 활성 2명: ${active.map(p=>p.nickname).join(', ')})`);

    a.close(); b.close(); c.close();
}

(async () => {
    try {
        await test1_leaveBeforeStart();
        await test2_closeRoomByHost();
        await test3_nonHostCantClose();
        await test4_hostLeaveClosesRoom();
        await test5_leaveDuringGame();
        console.log('\n━━━━━━━━━━━━━━━━━━━━━━━━');
        console.log('✅ 나가기/방닫기 전부 통과');
        process.exit(0);
    } catch (e) {
        console.error('\n❌ 실패:', e.message);
        process.exit(1);
    }
})();
