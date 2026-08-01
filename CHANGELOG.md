# Changelog

All notable changes to this project will be documented in this file.

## [1.2.0] - 2026-08-01

### Added
- Album cover art metadata (`coverart`) forwarded alongside title/artist/album through the full
  pipeline: `Player.pm` → `/lms/track` JSON → `LmsHttpAdapter` → `LmsBridge` → `HQPlayerClient`
  → `<PlayNextUri coverart="..."/>`.
  - For **local file** tracks: the plugin searches the same directory as the audio file for
    `cover.jpg`, `cover.png`, `folder.jpg`, or `folder.png` and sends the first match found.
  - For **streaming** tracks (e.g. Qobuz via LMS): the LMS built-in artwork endpoint
    (`http://<server>:<port>/music/<trackid>/cover.jpg`) is used so HQPlayer can display
    album art even for cloud sources.
- `TrackMetadata::coverart` field in `HQPlayerTypes.hpp`.
- Unit tests: `testPlayNextUriWithCoverArt`, `testPlayNextUriCoverArtOnly`.
- Integration test: `testTrackMetadataPassthrough` extended to verify `coverart` passthrough.

### Fixed
- **Qobuz authentication**: streaming proxy URLs that LMS constructs with `localhost` or
  `127.0.0.1` as the origin are now rewritten to the actual LMS server IP address
  (`Slim::Utils::Network::serverAddr()`) before being forwarded to HQPlayer Embedded.
  This ensures that HQPlayer — running on a separate host — can reach the LMS HTTP proxy
  and benefit from the Qobuz authentication performed by the Qobuz LMS plugin.

---

## [1.1.1] - 2026-07-28

### Changed
- `Plugins::HQPlayer::Player::load()` now forwards non-`file://` track URLs to `/lms/track` instead of rejecting them, allowing LMS-proxied streaming URIs (for example Qobuz via LMS) to be passed through to HQPlayer via `<PlayNextUri uri="..."/>`.
- README updated to document dual source modes for `/lms/track`: local filesystem paths and stream URIs.

---

## [1.1.0] - 2026-07-28
### Changed
- Track loading now uses a single `<PlayNextUri uri="..."/>` command instead of the previous two-command `<Load/>` + `<Play/>` sequence.  When HQPlayer is stopped the track starts immediately; when already playing it is queued for a gapless transition.
- Next/prev navigation is now handled entirely by the LMS Perl layer: `Player.pm::next()` and `Player.pm::prev()` call `playlist index +1/-1` on the LMS queue, which then triggers a new `load()` → `POST /lms/track` → `<PlayNextUri/>` cycle.  No HQPlayer XML skip commands are used.

### Added
- `IHQPlayerClient::playNextUri()` / `HQPlayerClient::playNextUri()` — sends `<PlayNextUri uri="..."/>` for gapless-capable track loading
- `Player.pm::next()` / `Player.pm::prev()` — LMS-layer skip handlers that advance the LMS queue and let `load()` handle the rest
- Unit tests: `testPlayNextUriCommandSendsCorrectXml`, `testPlayNextUriEmptyPathThrows`

### Removed
- `IHQPlayerClient::next()` / `HQPlayerClient::next()` — `<Next/>` XML command (superseded by LMS-queue navigation)
- `IHQPlayerClient::prev()` / `HQPlayerClient::prev()` — `<Prev/>` XML command (superseded by LMS-queue navigation)
- `LmsCommand::NextTrack` and `LmsCommand::PrevTrack`
- `POST /lms/next` and `POST /lms/prev` HTTP endpoints (now return 404)

---

## [1.0.0] - 2026-07-28

### Added
- `IHQPlayerClient::next()` / `HQPlayerClient::next()` — sends `<Next/>` to HQPlayer Embedded to skip to the next playlist track
- `IHQPlayerClient::prev()` / `HQPlayerClient::prev()` — sends `<Prev/>` to HQPlayer Embedded to skip to the previous playlist track
- `LmsCommand::NextTrack` and `LmsCommand::PrevTrack` — handled by `LmsBridge` with optimistic state `"playing"`
- `POST /lms/next` and `POST /lms/prev` HTTP endpoints
- `ILmsBridge::handleAlbumPlay(path)` / `LmsBridge::handleAlbumPlay(path)` — validates path and documents the TODO for native HQPlayer album/playlist-load support
- `POST /lms/album` HTTP endpoint — returns `400` for missing/empty path, `501 Not Implemented` until HQPlayer XML API exposes a native album-play command
- Unit tests: `testNextCommandSendsCorrectXml`, `testPrevCommandSendsCorrectXml` in `HQPlayerClientTests`
- Integration tests: next/prev state transitions and full `/lms/album` contract (valid path → 501, missing path → 400, empty body → 400)
- README endpoint reference table

## [0.9.0] - 2026-07-28

### Added
- `HQPlayerTypes.hpp` — `HQPlayerState` enum, `HQPlayerStatus` struct, `HQPlayerError` exception
- `IHQPlayerClient` — pure interface for controlling HQPlayer
- `HQPlayerClient` — TCP/XML client for HQPlayer Embedded port 4321 (one connection per command, configurable timeout)
- `HQPlayerSync` — background polling thread with configurable interval, observer callback, and exponential back-off on error
- `HQPlayerConfig` in `AppConfig` — `host`, `port` (4321), `timeout_ms` (3000), `poll_interval_ms` (5000) with YAML parsing
- `LmsBridge` wired to real `IHQPlayerClient`: forwards Play/Pause/Stop to HQPlayer, optimistic state updates, status maintained by HQPlayerSync
- `LmsBridge::updateCachedStatus()` — called by HQPlayerSync observer on each successful poll
- Unit tests: `HQPlayerClientTests` (mock TCP server), `HQPlayerSyncTests` (mock client), new `ConfigTests` covering `hqplayer:` section
- LMS plugin GUI: new **HQPlayer Embedded Connection** section (host, port, timeout_ms, poll_interval_ms) with live TCP reachability probe
- Plugin version bumped to 0.9.0

## [0.8.3] - 2026-07-28

### Added
- LMS HTTP adapter exposing `GET /lms/status`, `POST /lms/play`, `POST /lms/pause`, `POST /lms/stop`
- YAML config wiring for `logging.level`, `lms_adapter.host` (bind address with validation), and `lms_adapter.port`
- `Logger` utility with configurable log level (info/debug/warn/error)
- `Config` loader with startup-time validation of required fields
- `LmsBridge` stub implementing `ILmsBridge` interface
- `LmsHttpAdapter` with hardened lifecycle (start/stop, socket error handling)
- Unit tests for `Config` parsing and validation (`tests/unit/ConfigTests.cpp`)
- Integration tests for all four HTTP endpoints (`tests/integration/LmsHttpAdapterIntegrationTests.cpp`)
- CMake build with `hqplayer_core` library, daemon executable, and `ctest` test targets
- README with configure / build / run / curl smoke-test instructions
