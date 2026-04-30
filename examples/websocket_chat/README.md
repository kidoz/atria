# atria-websocket-chat

A small browser chat example for Atria WebSockets.

## Build

```bash
meson setup builddir -Dcatch2:tests=false
meson compile -C builddir
```

## Run

```bash
./builddir/examples/websocket_chat/atria-websocket-chat
```

Open `http://localhost:8082/` in two browser tabs. Both tabs join `/ws/lobby`; messages
typed in one tab are broadcast to every connected client in that room.

## Endpoints

| Method | Path | Description |
|---|---|---|
| GET | `/` | Browser chat page |
| GET | `/health` | Liveness check |
| WS | `/ws/{room}` | WebSocket room |
