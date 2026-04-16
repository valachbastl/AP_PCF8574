# Changelog

## [1.3.0] - 2026-04-16

### Added
- Thread-safe — FreeRTOS mutex chrání všechny I2C operace
- Retry logika — 3 pokusy s 2ms pauzou při I2C chybě, každý neúspěch zalogován
- `isOnline()` — probe dostupnosti zařízení bez změny cache
- Volitelný parametr `scl_hz` v konstruktoru (výchozí 100 kHz)
- Destruktor — uvolní `_dev` handle a mutex
- Kopírování zakázáno (`= delete`) — prevence sdílení handles
- Inicializační zápis v konstruktoru — čip je od startu ve známém stavu
- Validace čísla pinu (0–7) s logováním chyby
- Log tag obsahuje adresu zařízení (`PCF8574@0x20`) pro snadnou identifikaci

### Fixed
- `writePin()` — odstraněn zbytečný `readAll()` před zápisem (extra I2C transakce,
  při chybě čtení přepisoval `_data` nulami a mohl shodit ostatní výstupní piny)

## [1.2.1] - 2026-02-20

### Changed
- `writePin()` vraci `bool` - true pokud byl zapis proveden, false pokud byl preskocen

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
