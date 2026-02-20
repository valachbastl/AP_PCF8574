# Changelog

## [1.2.0] - 2026-02-20

### Changed
- `writePin()` preskoci I2C transakce pokud se stav pinu nezmenil (porovnani s cache pred readAll)

## [1.1.0] - 2026-02-18

### Added
- `setPinMode(uint8_t mask)` - nastaveni smeru pinu maskou (1=input, 0=output)
- `setPinMode(uint8_t pin, bool input)` - nastaveni smeru jednoho pinu
- Vstupni piny se pri kazdem `writeAll()` a `writePin()` automaticky drzi na HIGH

## [1.0.0] - 2026-02-13

### Added
- Inicializace PCF8574/PCF8574A pres ESP-IDF `i2c_master` API
- Cteni vsech 8 pinu najednou `readAll()`
- Cteni jednoho pinu `readPin(pin, fromCache)`
- Zapis vsech 8 pinu najednou `writeAll(value)`
- Zapis jednoho pinu `writePin(pin, state)` s read-modify-write
- Cache posledniho stavu `getCache()`
