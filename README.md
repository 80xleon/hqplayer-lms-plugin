# HQPlayer LMS Plugin

End-to-end integration that registers a virtual player in Lyrion Music Server
(LMS) and routes its audio through **HQPlayer Embedded** running on the same
or a remote PC.

## Architecture

```
LMS (Material skin)
  │  queue management, library browsing, transport UI
  │
  ▼  HTTP  (localhost:18080)
hqplayer_lms_daemon
  │  C++ adapter daemon
  │
  ▼  XML/TCP  (hqplayer-host:4321)
HQPlayer Embedded
     high-quality upsampling / DAC output
```

**Playback flow (per track)**

1. User selects the **HQPlayer** virtual player in Material skin and taps Play on an album.
2. LMS queues all tracks and calls `load()` on the virtual player with the first track source (local file or stream URL).
3. The Perl player sends `POST /lms/track {"path":"..."}` to the local daemon.
4. The daemon sends `<PlayNextUri uri="..."/>` to HQPlayer Embedded via XML/TCP.  When already playing, it first sends `<Stop/>` so the newly selected track starts immediately instead of queueing.
5. The daemon's status poller detects the `Playing → Stopped` transition when the track ends and sets `track_ended: true` in `GET /lms/status`.
6. The plugin's Perl polling timer reads `track_ended: true` and calls `playlist index +1` on the LMS queue.
7. LMS calls `load()` again with the next track — repeat from step 3.

**Source prerequisites**:
- **Local library files (`file://...`)**: the music library path must be
  accessible at the same absolute path on both the LMS host and the HQPlayer
  Embedded host (e.g. a shared NAS mount such as `/music` on both machines).
- **Streaming sources** (for example LMS-proxied URLs): LMS can pass a stream
  URI to HQPlayer via `/lms/track`; HQPlayer then opens that URI directly.

---

## 1) Configure YAML

Create `config.yaml`:

```yaml
logging:
  level: info

lms_adapter:
  host: 127.0.0.1
  port: 18080

hqplayer:
  host: 192.168.1.50   # hostname/IP of the machine running HQPlayer Embedded
  port: 4321           # HQPlayer XML control port (default 4321)
  timeout_ms: 3000
  poll_interval_ms: 5000
```

`lms_adapter.host` is the bind address for the daemon's HTTP server (must be
reachable from the LMS Perl process — use `127.0.0.1` when both run on the
same machine).

`hqplayer.host` is the hostname or IP of the HQPlayer Embedded instance.

---

## 2) Build

```bash
cmake -S . -B build
cmake --build build
```

---

## 3) Run daemon

```bash
./build/hqplayer_lms_daemon ./config.yaml
```

Press `ENTER` to stop.

### Run as a systemd service (recommended for production)

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

The service runs as the `squeezeboxserver` user by default.  To use a different
user, override it:

```bash
sudo systemctl edit hqplayer_lms_daemon
# Add:
# [Service]
# User=youruser
# Group=yourgroup
```

---

## 4) LMS Plugin Installation

The `plugin/` directory is a standard Lyrion Music Server plugin.  It adds:

- A **virtual player** that appears in the LMS player selector (Material skin
  and others) and routes playback to HQPlayer Embedded.
- A **configuration page** under **Settings → HQPlayer** to edit the daemon's
  YAML file from the LMS web interface.

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
| Virtual player name | `HQPlayer` | Name shown for the player in the LMS player selector. |

The top of the page also shows **live status indicators** — TCP probes to both
the daemon and HQPlayer Embedded — so you can verify connectivity at a glance.

Click **Save** to write `config.yaml` and then (re)start the daemon:

```bash
sudo systemctl restart hqplayer_lms_daemon
```

---

## 5) Curl smoke tests

```bash
curl -s http://127.0.0.1:18080/lms/status
curl -s -X POST http://127.0.0.1:18080/lms/play
curl -s -X POST http://127.0.0.1:18080/lms/pause
curl -s -X POST http://127.0.0.1:18080/lms/stop
curl -s -X POST http://127.0.0.1:18080/lms/track \
     -H 'Content-Type: application/json' \
     -d '{"path":"/music/Artist/Album/01.flac"}'
```

### Endpoint reference

| Method | Path | Description |
|--------|------|-------------|
| `GET`  | `/lms/status` | Returns current playback state as JSON.  Includes `track_ended: true` once after a Playing→Stopped transition. |
| `POST` | `/lms/play`   | Start or resume playback. |
| `POST` | `/lms/pause`  | Pause playback. |
| `POST` | `/lms/stop`   | Stop playback. |
| `POST` | `/lms/track`  | Load a source via `<PlayNextUri/>` (local file path or stream URI).  If HQPlayer is already playing, the daemon sends `<Stop/>` first so the new selection starts immediately.  Body: `{"path":"/absolute/path/to/file.flac"}` or `{"path":"http://lms-host:9000/..."}`.  Missing or empty `path` returns `400`.  HQPlayer errors return `502`. |
| `POST` | `/lms/album`  | **Not yet implemented** — returns `501 Not Implemented`. Will be enabled once the HQPlayer Embedded XML API exposes a native album/playlist-load command. |
