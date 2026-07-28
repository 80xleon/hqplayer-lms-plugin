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

hqplayer:
  host: 127.0.0.1   # hostname/IP of the machine running HQPlayer Embedded
  port: 4321         # HQPlayer XML control port (default 4321)
  timeout_ms: 3000   # TCP send/receive timeout in milliseconds
  poll_interval_ms: 5000  # how often the daemon polls HQPlayer for status
```

`lms_adapter.host` is used as the actual bind address and is validated at startup.
`hqplayer.host` is the hostname or IP of the HQPlayer Embedded instance.

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

## 3b) Run as a systemd service (recommended for production)

Copy the binary and service file:

```bash
sudo cp build/hqplayer_lms_daemon /usr/local/bin/
sudo mkdir -p /etc/hqplayer
sudo cp plugin/hqplayer_lms_daemon.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now hqplayer_lms_daemon
```

Check status:

```bash
sudo systemctl status hqplayer_lms_daemon
journalctl -u hqplayer_lms_daemon -f
```

After changing the configuration via the LMS plugin GUI, reload the service:

```bash
sudo systemctl restart hqplayer_lms_daemon
```

The service runs as the `squeezeboxserver` user by default. To use a different
user, override it:

```bash
sudo systemctl edit hqplayer_lms_daemon
# Add:
# [Service]
# User=youruser
# Group=yourgroup
```

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
| HQPlayer host | `127.0.0.1` | Hostname or IP of the machine running HQPlayer Embedded. |
| HQPlayer XML control port | `4321` | TCP port of the HQPlayer XML control API (default 4321). |
| Connection timeout (ms) | `3000` | Max time to wait for HQPlayer to respond (100–30000 ms). |
| Status poll interval (ms) | `5000` | How often the daemon queries HQPlayer for status (500–60000 ms). |

The top of the page also shows a **live status indicator** — the plugin makes a
quick TCP probe to `host:port` every time the settings page is loaded and
displays either **● Reachable** (daemon is up) or **○ Unreachable** (daemon is
not running or the configured address is wrong).

Click **Save** to write `config.yaml` and then start (or restart) the daemon:

```bash
sudo systemctl restart hqplayer_lms_daemon
```
