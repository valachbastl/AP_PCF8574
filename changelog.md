# Changelog

## [1.0.0] - 2026-02-13

### Added
- Inicializace PCF8574/PCF8574A pres ESP-IDF `i2c_master` API
- Cteni vsech 8 pinu najednou `readAll()`
- Cteni jednoho pinu `readPin(pin, fromCache)`
- Zapis vsech 8 pinu najednou `writeAll(value)`
- Zapis jednoho pinu `writePin(pin, state)` s read-modify-write
- Cache posledniho stavu `getCache()`
