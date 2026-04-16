#include "AP_PCF8574.h"
#include "freertos/task.h"

static constexpr int I2C_RETRIES    = 3;   // počet pokusů při I2C chybě
static constexpr int I2C_TIMEOUT_MS = 10;  // timeout jedné I2C transakce [ms]
static constexpr int RETRY_DELAY_MS = 2;   // prodleva mezi pokusy [ms]
static constexpr int MUTEX_WAIT_MS  = 50;  // timeout čekání na mutex [ms]

// =============================================================================
// Konstruktor / destruktor
// =============================================================================

AP_PCF8574::AP_PCF8574(i2c_master_bus_handle_t bus, uint8_t address, uint32_t scl_hz)
    : _data(0xFF), _inputMask(0xFF)
{
    snprintf(_tag, sizeof(_tag), "PCF8574@0x%02X", address);

    _mutex = xSemaphoreCreateMutex();
    if (_mutex == nullptr) {
        ESP_LOGE(_tag, "Nepodařilo se vytvořit mutex");
        abort();
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address  = address;
    dev_cfg.scl_speed_hz    = scl_hz;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &_dev));

    // Inicializační zápis — všechny piny HIGH (input ready, output neaktivní)
    _write(_data);
}

AP_PCF8574::~AP_PCF8574()
{
    if (_dev)   i2c_master_bus_rm_device(_dev);
    if (_mutex) vSemaphoreDelete(_mutex);
}

// =============================================================================
// Privátní I2C pomocníky s retry logikou
// =============================================================================

void AP_PCF8574::_write(uint8_t data)
{
    for (int attempt = 1; attempt <= I2C_RETRIES; attempt++) {
        esp_err_t err = i2c_master_transmit(_dev, &data, 1, I2C_TIMEOUT_MS);
        if (err == ESP_OK) {
            _data = data;
            return;
        }
        ESP_LOGW(_tag, "Zápis selhal (pokus %d/%d): %s", attempt, I2C_RETRIES, esp_err_to_name(err));
        if (attempt < I2C_RETRIES) vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
    }
    ESP_LOGE(_tag, "Zápis selhal po %d pokusech — stav výstupů nezměněn", I2C_RETRIES);
}

bool AP_PCF8574::_read(uint8_t &data)
{
    for (int attempt = 1; attempt <= I2C_RETRIES; attempt++) {
        esp_err_t err = i2c_master_receive(_dev, &data, 1, I2C_TIMEOUT_MS);
        if (err == ESP_OK) {
            _data = data;
            return true;
        }
        ESP_LOGW(_tag, "Čtení selhalo (pokus %d/%d): %s", attempt, I2C_RETRIES, esp_err_to_name(err));
        if (attempt < I2C_RETRIES) vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
    }
    ESP_LOGE(_tag, "Čtení selhalo po %d pokusech", I2C_RETRIES);
    return false;
}

// =============================================================================
// Veřejné API
// =============================================================================

void AP_PCF8574::setPinMode(uint8_t mask)
{
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(MUTEX_WAIT_MS)) != pdTRUE) {
        ESP_LOGE(_tag, "setPinMode: mutex timeout");
        return;
    }
    _inputMask = mask;
    _write(_data | _inputMask);  // vstupní piny → HIGH, výstupní beze změny
    xSemaphoreGive(_mutex);
}

void AP_PCF8574::setPinMode(uint8_t pin, bool input)
{
    if (pin > 7) { ESP_LOGE(_tag, "setPinMode: neplatný pin %d", pin); return; }
    uint8_t newMask = input ? (_inputMask | (1 << pin)) : (_inputMask & ~(1 << pin));
    setPinMode(newMask);
}

uint8_t AP_PCF8574::readAll()
{
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(MUTEX_WAIT_MS)) != pdTRUE) {
        ESP_LOGE(_tag, "readAll: mutex timeout");
        return _data;  // vrátí cache
    }
    uint8_t value = _data;
    _read(value);  // při chybě zůstane value = _data (cache)
    xSemaphoreGive(_mutex);
    return value;
}

bool AP_PCF8574::readPin(uint8_t pin, bool fromCache)
{
    if (pin > 7) { ESP_LOGE(_tag, "readPin: neplatný pin %d", pin); return false; }
    if (fromCache) return (_data >> pin) & 0x01;
    return (readAll() >> pin) & 0x01;
}

void AP_PCF8574::writeAll(uint8_t value)
{
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(MUTEX_WAIT_MS)) != pdTRUE) {
        ESP_LOGE(_tag, "writeAll: mutex timeout");
        return;
    }
    _write(value | _inputMask);  // vstupní piny vždy HIGH
    xSemaphoreGive(_mutex);
}

bool AP_PCF8574::writePin(uint8_t pin, bool state)
{
    if (pin > 7) { ESP_LOGE(_tag, "writePin: neplatný pin %d", pin); return false; }
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(MUTEX_WAIT_MS)) != pdTRUE) {
        ESP_LOGE(_tag, "writePin: mutex timeout");
        return false;
    }

    uint8_t newData = state ? (_data | (1 << pin)) : (_data & ~(1 << pin));
    newData |= _inputMask;  // vstupní piny vždy HIGH

    bool written = false;
    if (newData != _data) {
        _write(newData);
        written = true;
    }

    xSemaphoreGive(_mutex);
    return written;
}

uint8_t AP_PCF8574::getCache() const
{
    return _data;
}

bool AP_PCF8574::isOnline()
{
    uint8_t dummy;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(MUTEX_WAIT_MS)) != pdTRUE) return false;
    bool ok = (i2c_master_receive(_dev, &dummy, 1, I2C_TIMEOUT_MS) == ESP_OK);
    xSemaphoreGive(_mutex);
    return ok;
}
