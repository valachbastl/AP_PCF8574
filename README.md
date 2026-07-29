# AP_PCF8574

PCF8574/PCF8574A I2C 8-bit I/O expander driver for ESP-IDF.

## Features

- Uses ESP-IDF `i2c_master` API (new driver, not legacy)
- Constructor never touches the bus and never aborts the app — registration
  happens in `begin()`, same as the other AP_ sensor drivers
- If the device doesn't respond, `begin()` reports the error but the app
  keeps running — behaves like a disconnected sensor, not a crash
- Registration is retried automatically and transparently by every other
  method (rate-limited) if it hasn't succeeded yet — no need to re-call
  `begin()` from a timer by hand
- `addReconnectListener()` / `addDisconnectListener()` fire on offline↔online
  transitions, detected from the normal calls the app already makes (no
  extra I2C traffic) — typical use: re-apply pin mode / output state after
  a reconnect, since the chip forgets everything across its own power cycle.
  Support any number of listeners (intrusive linked list — no heap
  allocation, no fixed cap), for a single instance shared across
  independent tasks (e.g. a servo valve task and a separate pulse-counter
  task both watching the same chip)
- Thread-safe — FreeRTOS mutex, safe to use from multiple tasks
- Retry logic — 3 attempts with 2ms delay on I2C error
- Writes are verified by an immediate read-back of the output pins (input
  pins are excluded — they reflect real external levels, not what was
  written) — a mismatch is retried like a failed attempt, guarding against
  silent bit corruption on a noisy bus
- Log rate-limiting under sustained failure — full detail for the first few
  failures, then a periodic reminder only, so a long unattended outage
  (e.g. device unplugged for days) doesn't flood the log
- Pin direction configuration (input/output) with mask or per-pin
- Input pins automatically held HIGH during write operations
- Read/write individual pins or all 8 pins at once
- Cached state for read without I2C communication
- Read-modify-write for single pin operations, skips I2C if state unchanged
- `isOnline()` for runtime device availability check
- Configurable SCL speed
- Supports PCF8574 (0x20-0x27) and PCF8574A (0x38-0x3F)

## Installation

### PlatformIO

Add to `platformio.ini`:

```ini
lib_deps =
    https://github.com/valachbastl/AP_PCF8574.git
```

Or with specific version:

```ini
lib_deps =
    https://github.com/valachbastl/AP_PCF8574.git#v1.4.0
```

## Usage

### Initialization

```cpp
#include "driver/i2c_master.h"
#include "AP_PCF8574.h"

// I2C bus must be initialized first
i2c_master_bus_handle_t i2c_bus;
// ... i2c_new_master_bus(&bus_config, &i2c_bus);

AP_PCF8574 pcf(i2c_bus, 0x20);  // address 0x20 (A0-A2 = GND), constructor only stores params

esp_err_t ret = pcf.begin();
if (ret != ESP_OK) {
    ESP_LOGW(TAG, "PCF8574 not responding: %s", esp_err_to_name(ret));
    // app keeps running - readAll() etc. return fail-safe values until
    // registration succeeds, which every later call retries automatically
}

// Optional: react to the device (re)appearing / disappearing, e.g. to
// re-apply pin mode and output defaults after a power cycle (the chip
// forgets its state - power-on default is all pins HIGH). Listener nodes
// are owned by the caller (no heap allocation in the library) - keep them
// alive for as long as they're registered, e.g. as local/member variables
// alongside pcf itself.
AP_PCF8574::Listener reconnectListener([&pcf] {
    ESP_LOGW(TAG, "PCF8574 (re)connected - reapplying config");
    pcf.setPinMode(0b00000111);
});
pcf.addReconnectListener(reconnectListener);

AP_PCF8574::Listener disconnectListener([] {
    ESP_LOGE(TAG, "PCF8574 stopped responding");
});
pcf.addDisconnectListener(disconnectListener);
```

### Pin Direction

```cpp
// Set pin direction by mask (1=input, 0=output)
// Default is 0xFF (all inputs)
pcf.setPinMode(0b00000111);  // P0-P2 input, P3-P7 output

// Set single pin direction
pcf.setPinMode(3, false);  // P3 as output
pcf.setPinMode(0, true);   // P0 as input
```

### Reading Inputs

```cpp
// Read all 8 pins
uint8_t data = pcf.readAll();
bool pin0 = (data >> 0) & 0x01;

// Read single pin (with I2C read)
bool pin3 = pcf.readPin(3);

// Read single pin from cache (no I2C communication)
bool pin3_cached = pcf.readPin(3, true);

// Detect a stale (cached, not fresh) reading without a separate isOnline()
// probe - same I2C transaction, no extra bus traffic
bool stale = false;
bool pin3_checked = pcf.readPin(3, false, &stale);
if (stale) {
    // value is the last known state, not a confirmed current reading
}
```

### Writing Outputs

```cpp
// Write all 8 pins at once
// Input pins are automatically kept HIGH
pcf.writeAll(0xFF);

// Write single pin (read-modify-write)
// Input pins are automatically kept HIGH
pcf.writePin(4, false);  // set pin 4 LOW
pcf.writePin(5, true);   // set pin 5 HIGH

// Confirm the device actually accepted the write (e.g. before trusting a
// motor command was applied)
bool ok = false;
pcf.writePin(4, false, &ok);
if (!ok) {
    // device didn't confirm - I2C error, don't assume the pin changed
}
```

### Device Availability Check

```cpp
if (!pcf.isOnline()) {
    ESP_LOGE(TAG, "PCF8574 nereaguje!");
    // error handling...
}
```

### Using with INT Pin

```cpp
// PCF8574 INT pin triggers on any input change (active LOW)
static volatile bool pcf_int_fired = true;

static void IRAM_ATTR pcf_int_isr(void *arg)
{
    pcf_int_fired = true;
}

// In task loop:
if (pcf_int_fired) {
    pcf_int_fired = false;
    uint8_t data = pcf.readAll();
    // process inputs...
}
```

## API Reference

| Method | Description |
|--------|-------------|
| `AP_PCF8574(bus, address, scl_hz)` | Constructor - stores params only, scl_hz optional (default 100 kHz) |
| `begin()` | Registers device on I2C bus + writes initial state, returns `esp_err_t`. Idempotent |
| `setPinMode(mask)` | Set pin direction by mask (1=input, 0=output) |
| `setPinMode(pin, input)` | Set single pin direction |
| `readAll(stale)` | Read all 8 pins, returns uint8_t. Optional `bool *stale` set to true if the value is a cached fallback, not a fresh reading |
| `readPin(pin, fromCache, stale)` | Read single pin (0-7), fromCache default false, optional `bool *stale` as above |
| `writeAll(value, ok)` | Write all 8 pins (input pins kept HIGH). Optional `bool *ok` set to true only if the device confirmed the write |
| `writePin(pin, state, ok)` | Write single pin, returns true if write was performed. Optional `bool *ok` set to true if the device confirmed the new state (written+verified, or already matched) |
| `getCache()` | Get last read/written state without I2C |
| `isOnline()` | Returns true if device responds on I2C bus. Also drives the lazy registration retry, same as any other method |
| `addReconnectListener(Listener&)` | Registers a listener fired when the device transitions from offline to online. Caller owns the `Listener` node (must outlive registration) - no heap allocation, no cap on how many |
| `addDisconnectListener(Listener&)` | Registers a listener fired when the device transitions from online to offline. Same ownership rules as `addReconnectListener` |

## Author

Petr Adámek

## License

MIT — see [LICENSE](LICENSE).
