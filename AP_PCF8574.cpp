#include "AP_PCF8574.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"

static constexpr int     I2C_RETRIES              = 3;        // number of attempts on I2C error
static constexpr int     I2C_TIMEOUT_MS            = 10;       // timeout of a single I2C transaction [ms]
static constexpr int     RETRY_DELAY_MS            = 2;        // delay between attempts [ms]
static constexpr int     MUTEX_WAIT_MS             = 50;       // timeout waiting for the mutex [ms]
static constexpr int64_t BEGIN_RETRY_INTERVAL_US   = 2000000;  // lazy begin() retry cadence while unregistered [us]

// =============================================================================
// Constructor / destructor
// =============================================================================

AP_PCF8574::AP_PCF8574(i2c_master_bus_handle_t bus, uint8_t address, uint32_t scl_hz)
    : _bus(bus), _address(address), _scl_hz(scl_hz), _dev(nullptr),
      _data(0xFF), _inputMask(0xFF)
{
    snprintf(_tag, sizeof(_tag), "PCF8574@0x%02X", address);

    // Statically allocated mutex (StaticSemaphore_t member instead of heap) -
    // no heap, deterministic. Unlike xSemaphoreCreateMutex(), this cannot
    // fail due to OOM.
    _mutex = xSemaphoreCreateMutexStatic(&_mutexBuffer);
}

AP_PCF8574::~AP_PCF8574()
{
    if (_dev)   i2c_master_bus_rm_device(_dev);
    if (_mutex) vSemaphoreDelete(_mutex);
}

// =============================================================================
// Registration (begin() and the lazy retry used by every other method)
// =============================================================================

esp_err_t AP_PCF8574::_tryBegin()
{
    // Caller holds _mutex; assumes _dev == nullptr.
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address  = _address;
    dev_cfg.scl_speed_hz    = _scl_hz;
    esp_err_t ret = i2c_master_bus_add_device(_bus, &dev_cfg, &_dev);
    if (ret != ESP_OK) {
        _dev = nullptr;
        return ret;
    }

    // Initial write - all pins HIGH (inputs ready, outputs inactive). Also
    // serves as a probe for device presence at the address: if it doesn't
    // respond, the just-registered handle is removed again. Updates
    // _online via _write()'s own bookkeeping.
    ret = _write(_withInputsHigh(_data));
    if (ret != ESP_OK) {
        i2c_master_bus_rm_device(_dev);
        _dev = nullptr;
        return ret;
    }
    return ESP_OK;
}

bool AP_PCF8574::_ensureBegun()
{
    // Caller holds _mutex.
    if (_dev != nullptr) return true;

    int64_t now = esp_timer_get_time();
    if (_lastBeginAttemptUs != 0 && (now - _lastBeginAttemptUs) < BEGIN_RETRY_INTERVAL_US) {
        return false;  // too soon since the last attempt - don't hammer the bus
    }
    _lastBeginAttemptUs = now;
    return _tryBegin() == ESP_OK;
}

esp_err_t AP_PCF8574::begin()
{
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(MUTEX_WAIT_MS)) != pdTRUE) {
        ESP_LOGE(_tag, "begin: mutex timeout");
        return ESP_ERR_TIMEOUT;
    }

    if (_dev != nullptr) {
        xSemaphoreGive(_mutex);
        return ESP_OK;  // already registered
    }

    bool wasOnline = _online;  // always false here, kept for symmetry with the other methods
    _lastBeginAttemptUs = esp_timer_get_time();  // explicit call - always attempts now, never rate-limited
    esp_err_t ret = _tryBegin();
    bool justReconnected = !wasOnline && _online;

    xSemaphoreGive(_mutex);
    if (justReconnected) _fireReconnect();
    return ret;
}

// =============================================================================
// Private I2C helpers with retry logic
// =============================================================================

esp_err_t AP_PCF8574::_write(uint8_t data)
{
    if (_dev == nullptr) return ESP_ERR_INVALID_STATE;

    // Rate-limit logging under sustained failure (e.g. device unplugged for a
    // long time) - full detail for the first few failures, then a periodic
    // reminder only, so a long-running unattended failure doesn't flood the log.
    bool verbose = _consecutiveFailures < 5 || _consecutiveFailures % 100 == 0;

    // Only output-pin bits are meaningful to verify - input-pin bits reflect
    // the real external electrical level (quasi-bidirectional I/O), not the
    // arbitrary HIGH written for them, so they're excluded from comparison.
    uint8_t outputMask = (uint8_t)~_inputMask;

    for (int attempt = 1; attempt <= I2C_RETRIES; attempt++) {
        esp_err_t err = i2c_master_transmit(_dev, &data, 1, I2C_TIMEOUT_MS);
        if (err == ESP_OK) {
            uint8_t readback = 0;
            esp_err_t verifyErr = i2c_master_receive(_dev, &readback, 1, I2C_TIMEOUT_MS);
            if (verifyErr == ESP_OK && (readback & outputMask) == (data & outputMask)) {
                _data                = data;
                _consecutiveFailures = 0;
                _online              = true;
                return ESP_OK;
            }
            if (verifyErr == ESP_OK) {
                err = ESP_ERR_INVALID_RESPONSE;
                if (verbose) {
                    ESP_LOGW(_tag, "Write verify mismatch (attempt %d/%d): wrote 0x%02X, read back 0x%02X (output bits)",
                             attempt, I2C_RETRIES, data & outputMask, readback & outputMask);
                }
            } else {
                err = verifyErr;
                if (verbose) {
                    ESP_LOGW(_tag, "Write verify read failed (attempt %d/%d): %s", attempt, I2C_RETRIES, esp_err_to_name(err));
                }
            }
        } else if (verbose) {
            ESP_LOGW(_tag, "Write failed (attempt %d/%d): %s", attempt, I2C_RETRIES, esp_err_to_name(err));
        }

        if (attempt < I2C_RETRIES) {
            vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
        } else {
            _consecutiveFailures++;
            _online = false;
            if (verbose) {
                ESP_LOGE(_tag, "Write failed after %d attempts — output state unchanged", I2C_RETRIES);
            }
            return err;
        }
    }
    return ESP_FAIL;  // unreachable, just for completeness of return paths
}

esp_err_t AP_PCF8574::_read(uint8_t &data)
{
    if (_dev == nullptr) return ESP_ERR_INVALID_STATE;

    bool verbose = _consecutiveFailures < 5 || _consecutiveFailures % 100 == 0;

    for (int attempt = 1; attempt <= I2C_RETRIES; attempt++) {
        // Scratch buffer, not `data` directly - a failing/noisy transfer (e.g.
        // bus glitch during a brown-out) can still clock in a partial byte via
        // the ISR before the driver reports the overall transaction as failed,
        // which would silently corrupt an already-good cache value sitting in
        // `data`. Only commit to `data`/`_data` on a confirmed ESP_OK, so a
        // failed read reliably leaves the caller's fail-safe cache untouched.
        uint8_t rx  = 0;
        esp_err_t err = i2c_master_receive(_dev, &rx, 1, I2C_TIMEOUT_MS);
        if (err == ESP_OK) {
            data                 = rx;
            _data                = rx;
            _consecutiveFailures = 0;
            _online              = true;
            return ESP_OK;
        }
        if (verbose) {
            ESP_LOGW(_tag, "Read failed (attempt %d/%d): %s", attempt, I2C_RETRIES, esp_err_to_name(err));
        }
        if (attempt < I2C_RETRIES) {
            vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
        } else {
            _consecutiveFailures++;
            _online = false;
            if (verbose) {
                ESP_LOGE(_tag, "Read failed after %d attempts", I2C_RETRIES);
            }
            return err;
        }
    }
    return ESP_FAIL;  // unreachable, just for completeness of return paths
}

uint8_t AP_PCF8574::_withInputsHigh(uint8_t value) const
{
    return value | _inputMask;
}

void AP_PCF8574::_fireReconnect()
{
    // Snapshot just the head pointer under the mutex, then walk it unlocked -
    // see the header comment on _fireReconnect/_fireDisconnect for why this
    // is safe without holding the lock for the whole walk.
    Listener *node = nullptr;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(MUTEX_WAIT_MS)) == pdTRUE) {
        node = _reconnectListeners;
        xSemaphoreGive(_mutex);
    }
    for (; node != nullptr; node = node->_next) {
        if (node->_callback) node->_callback();
    }
}

void AP_PCF8574::_fireDisconnect()
{
    Listener *node = nullptr;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(MUTEX_WAIT_MS)) == pdTRUE) {
        node = _disconnectListeners;
        xSemaphoreGive(_mutex);
    }
    for (; node != nullptr; node = node->_next) {
        if (node->_callback) node->_callback();
    }
}

// =============================================================================
// Public API
// =============================================================================

void AP_PCF8574::setPinMode(uint8_t mask)
{
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(MUTEX_WAIT_MS)) != pdTRUE) {
        ESP_LOGE(_tag, "setPinMode: mutex timeout");
        return;
    }
    bool wasOnline = _online;
    _ensureBegun();
    _inputMask = mask;
    _write(_withInputsHigh(_data));  // input pins -> HIGH, output pins unchanged
    bool justReconnected  = !wasOnline && _online;
    bool justDisconnected = wasOnline && !_online;
    xSemaphoreGive(_mutex);
    if (justReconnected)  _fireReconnect();
    if (justDisconnected) _fireDisconnect();
}

void AP_PCF8574::setPinMode(uint8_t pin, bool input)
{
    if (pin > 7) { ESP_LOGE(_tag, "setPinMode: invalid pin %d", pin); return; }
    uint8_t newMask = input ? (_inputMask | (1 << pin)) : (_inputMask & ~(1 << pin));
    setPinMode(newMask);
}

uint8_t AP_PCF8574::readAll(bool *stale)
{
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(MUTEX_WAIT_MS)) != pdTRUE) {
        ESP_LOGE(_tag, "readAll: mutex timeout");
        if (stale) *stale = true;
        return _data;  // return cache
    }
    bool wasOnline = _online;
    _ensureBegun();  // lazy, rate-limited retry if not registered yet
    uint8_t value = _data;
    esp_err_t err = _read(value);  // on error (I2C or still unregistered) stays value = _data (cache)
    if (stale) *stale = (err != ESP_OK);
    bool justReconnected  = !wasOnline && _online;
    bool justDisconnected = wasOnline && !_online;
    xSemaphoreGive(_mutex);
    if (justReconnected)  _fireReconnect();
    if (justDisconnected) _fireDisconnect();
    return value;
}

bool AP_PCF8574::readPin(uint8_t pin, bool fromCache, bool *stale)
{
    if (pin > 7) {
        ESP_LOGE(_tag, "readPin: invalid pin %d", pin);
        if (stale) *stale = true;
        return false;
    }
    if (fromCache) {
        if (stale) *stale = false;  // cache read was explicitly requested, not a fallback
        return (getCache() >> pin) & 0x01;
    }
    return (readAll(stale) >> pin) & 0x01;
}

void AP_PCF8574::writeAll(uint8_t value, bool *ok)
{
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(MUTEX_WAIT_MS)) != pdTRUE) {
        ESP_LOGE(_tag, "writeAll: mutex timeout");
        if (ok) *ok = false;
        return;
    }
    bool wasOnline = _online;
    _ensureBegun();
    esp_err_t err = _write(_withInputsHigh(value));  // input pins always HIGH
    if (ok) *ok = (err == ESP_OK);
    bool justReconnected  = !wasOnline && _online;
    bool justDisconnected = wasOnline && !_online;
    xSemaphoreGive(_mutex);
    if (justReconnected)  _fireReconnect();
    if (justDisconnected) _fireDisconnect();
}

bool AP_PCF8574::writePin(uint8_t pin, bool state, bool *ok)
{
    if (pin > 7) {
        ESP_LOGE(_tag, "writePin: invalid pin %d", pin);
        if (ok) *ok = false;
        return false;
    }
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(MUTEX_WAIT_MS)) != pdTRUE) {
        ESP_LOGE(_tag, "writePin: mutex timeout");
        if (ok) *ok = false;
        return false;
    }

    bool wasOnline = _online;
    _ensureBegun();

    uint8_t newData = state ? (_data | (1 << pin)) : (_data & ~(1 << pin));
    newData = _withInputsHigh(newData);  // input pins always HIGH

    bool written = false;
    esp_err_t err = ESP_OK;
    if (newData != _data) {
        err = _write(newData);
        written = true;
    }
    if (ok) *ok = (err == ESP_OK);  // skipped write (state already matched) counts as ok

    bool justReconnected  = !wasOnline && _online;
    bool justDisconnected = wasOnline && !_online;
    xSemaphoreGive(_mutex);
    if (justReconnected)  _fireReconnect();
    if (justDisconnected) _fireDisconnect();
    return written;
}

uint8_t AP_PCF8574::getCache() const
{
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(MUTEX_WAIT_MS)) != pdTRUE) {
        return _data;  // best-effort fallback on mutex timeout
    }
    uint8_t value = _data;
    xSemaphoreGive(_mutex);
    return value;
}

bool AP_PCF8574::isOnline()
{
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(MUTEX_WAIT_MS)) != pdTRUE) return false;

    bool wasOnline = _online;
    _ensureBegun();

    bool ok = false;
    if (_dev != nullptr) {
        uint8_t dummy;
        ok      = (i2c_master_receive(_dev, &dummy, 1, I2C_TIMEOUT_MS) == ESP_OK);
        _online = ok;
    }

    bool justReconnected  = !wasOnline && _online;
    bool justDisconnected = wasOnline && !_online;
    xSemaphoreGive(_mutex);
    if (justReconnected)  _fireReconnect();
    if (justDisconnected) _fireDisconnect();
    return ok;
}

void AP_PCF8574::addReconnectListener(Listener &listener)
{
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(MUTEX_WAIT_MS)) != pdTRUE) {
        ESP_LOGE(_tag, "addReconnectListener: mutex timeout");
        return;
    }
    listener._next      = _reconnectListeners;  // prepend - O(1), no allocation
    _reconnectListeners = &listener;
    xSemaphoreGive(_mutex);
}

void AP_PCF8574::addDisconnectListener(Listener &listener)
{
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(MUTEX_WAIT_MS)) != pdTRUE) {
        ESP_LOGE(_tag, "addDisconnectListener: mutex timeout");
        return;
    }
    listener._next       = _disconnectListeners;
    _disconnectListeners = &listener;
    xSemaphoreGive(_mutex);
}
