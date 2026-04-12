// Thin wrapper around WebSocket. Emits events by type.
const WS_URL = window.IP_WS_URL || "ws://localhost:8080/";

export class WSClient {
    constructor() {
        this.ws = null;
        this.handlers = new Map();
    }

    open() {
        this.ws = new WebSocket(WS_URL);
        this.ws.onmessage = (e) => {
            const msg = JSON.parse(e.data);
            const list = this.handlers.get(msg.type) || [];
            for (const fn of list) fn(msg);
        };
        return new Promise((resolve, reject) => {
            this.ws.onopen = () => resolve();
            this.ws.onerror = () => reject(new Error("WebSocket error"));
        });
    }

    send(obj) { this.ws.send(JSON.stringify(obj)); }

    on(type, fn) {
        if (!this.handlers.has(type)) this.handlers.set(type, []);
        this.handlers.get(type).push(fn);
    }
}
