# AP_PCF8574

PCF8574/PCF8574A I2C 8-bit I/O expander driver for ESP-IDF.

## Features

- Uses ESP-IDF `i2c_master` API (new driver, not legacy)
- Pin direction configuration (input/output) with mask or per-pin
- Input pins automatically held HIGH during write operations
- Read/write individual pins or all 8 pins at once
- Cached state for read without I2C communication
- Read-modify-write for single pin operations
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
    https://github.com/valachbastl/AP_PCF8574.git#v1.1.0
```

## Usage

### Initialization

```cpp
#include "driver/i2c_master.h"
#include "AP_PCF8574.h"

// I2C bus must be initialized first
i2c_master_bus_handle_t i2c_bus;
// ... i2c_new_master_bus(&bus_config, &i2c_bus);

AP_PCF8574 pcf(i2c_bus, 0x20);  // address 0x20 (A0-A2 = GND)
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
| `AP_PCF8574(bus, address)` | Constructor - adds device to I2C bus |
| `setPinMode(mask)` | Set pin direction by mask (1=input, 0=output) |
| `setPinMode(pin, input)` | Set single pin direction |
| `readAll()` | Read all 8 pins, returns uint8_t |
| `readPin(pin, fromCache)` | Read single pin (0-7), fromCache default false |
| `writeAll(value)` | Write all 8 pins (input pins kept HIGH) |
| `writePin(pin, state)` | Write single pin with read-modify-write |
| `getCache()` | Get last read/written state without I2C |

## Author

Petr Adámek
