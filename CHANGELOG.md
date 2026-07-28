# Changelog

All notable changes to this project will be documented in this file.

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
