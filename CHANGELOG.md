# Changelog

All notable changes to this project will be documented in this file.

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
