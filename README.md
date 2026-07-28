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

## 5) LMS Plugin Installation

The `plugin/` directory is a standard Lyrion Music Server plugin that adds a
configuration GUI so you can edit the daemon's YAML file directly from the LMS
web interface.

### Install

1. Copy (or symlink) the `plugin/` directory into your LMS plugins folder and
   name it `HQPlayer`:

   ```bash
   cp -r plugin/ /var/lib/squeezeboxserver/Plugins/HQPlayer
   # or on macOS:
   cp -r plugin/ ~/Library/Application\ Support/Squeezebox/Plugins/HQPlayer
   ```

2. Restart LMS.

3. Open the LMS web interface, navigate to **Settings → Plugins**, and enable
   **HQPlayer** if it is not already enabled.

4. Go to **Settings → HQPlayer** to open the configuration page.

### Configuration page fields

| Field | Default | Description |
|---|---|---|
| Config file path | `/etc/hqplayer/config.yaml` | Absolute path where the plugin writes the YAML file. |
| Bind host | `127.0.0.1` | IP address the daemon binds to (IPv4 or IPv6). |
| Port | `18080` | TCP port for the daemon's HTTP server (1–65535). |
| Log level | `info` | Daemon log verbosity: `trace`, `debug`, `info`, `warn`, `error`. |

Click **Save** to write `config.yaml` and then start (or restart) the daemon:

```bash
./build/hqplayer_lms_daemon /etc/hqplayer/config.yaml
```
