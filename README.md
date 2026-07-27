# HQPlayer LMS Plugin Adapter

Minimal production-ready LMS HTTP adapter scaffold exposing:

- `GET /lms/status`
- `POST /lms/play`
- `POST /lms/pause`
- `POST /lms/stop`

## 1) Configure YAML

Create `config.yaml`:

```yaml
logging:
  level: info

lms_adapter:
  host: 127.0.0.1
  port: 18080
```

`lms_adapter.host` is used as the actual bind address and is validated at startup.

## 2) Build

```bash
cmake -S . -B build
cmake --build build
```

## 3) Run daemon

```bash
./build/hqplayer_lms_daemon ./config.yaml
```

Press `ENTER` to stop.

## 4) Curl smoke tests

```bash
curl -s http://127.0.0.1:18080/lms/status
curl -s -X POST http://127.0.0.1:18080/lms/play
curl -s -X POST http://127.0.0.1:18080/lms/pause
curl -s -X POST http://127.0.0.1:18080/lms/stop
```
