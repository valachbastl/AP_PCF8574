#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <cstdint>
#include <functional>

/**
 * @brief Driver for PCF8574 / PCF8574A — 8-bit I2C GPIO expander
 *
 * Thread-safe, retry logic for transient I2C errors, input pins are
 * permanently held HIGH (quasi-bidirectional I/O on PCF8574).
 */
class AP_PCF8574
{
public:
    /**
     * @brief Constructor — only stores parameters, does not touch the bus
     * @param bus     I2C master bus handle (from i2c_new_master_bus)
     * @param address I2C address (0x20-0x27 for PCF8574, 0x38-0x3F for PCF8574A)
     * @param scl_hz  SCL speed in Hz (default 100 kHz)
     */
    AP_PCF8574(i2c_master_bus_handle_t bus, uint8_t address, uint32_t scl_hz = 100000);
    ~AP_PCF8574();

    // Owns an I2C device handle (_dev) -> non-copyable (prevents double
    // use / double release of the same handle).
    AP_PCF8574(const AP_PCF8574 &) = delete;
    AP_PCF8574 &operator=(const AP_PCF8574 &) = delete;

    /**
     * @brief Registers the device on the I2C bus and writes the initial
     *        state (current pin mask, inputs HIGH). Must be called before
     *        first use. Idempotent — calling again after success is a no-op.
     *        If the device doesn't respond at the address, returns an
     *        error but does not abort the app — behaves like a disconnected
     *        sensor: other methods keep working and return fail-safe values
     *        (cache / false), without I2C communication.
     *
     *        Registration is retried automatically and transparently on
     *        every later call to any other method (rate-limited — at most
     *        once every couple of seconds, so a long-absent device doesn't
     *        get hammered) - there's no need to call begin() again by hand.
     *        Calling it again explicitly still works and is never
     *        rate-limited (an explicit call means the caller wants an
     *        immediate attempt, e.g. right after wiring up power).
     * @return ESP_OK on success, otherwise the error from
     *         i2c_master_bus_add_device or from the initial write (device
     *         not responding at the address)
     */
    esp_err_t begin();

    /**
     * @brief Sets pin direction by mask (1=input, 0=output)
     *        Input pins are automatically held HIGH on every write.
     *        Default: 0xFF (all input)
     */
    void setPinMode(uint8_t mask);

    /**
     * @brief Sets the direction of a single pin
     * @param pin   Pin number (0-7)
     * @param input true = input, false = output
     */
    void setPinMode(uint8_t pin, bool input);

    /**
     * @brief Reads all 8 pins at once over I2C
     * @param stale if non-null, set to true when the returned value is a
     *              stale cache (I2C error, mutex timeout, or the device
     *              isn't registered yet) rather than a fresh reading
     * @return 8-bit port value; on I2C error (or if not registered yet)
     *         returns the last known value from cache — fail-safe
     */
    uint8_t readAll(bool *stale = nullptr);

    /**
     * @brief Reads the state of a single pin
     * @param pin       Pin number (0-7)
     * @param fromCache true = return the last read state without I2C communication
     * @param stale     if non-null, set to true when the value is a stale
     *                  cache rather than a fresh reading. Always false when
     *                  fromCache is true (staleness doesn't apply - a cache
     *                  read was explicitly requested) or on invalid pin.
     * @return true = HIGH, false = LOW
     */
    bool readPin(uint8_t pin, bool fromCache = false, bool *stale = nullptr);

    /**
     * @brief Writes all 8 pins at once
     *        Input pins (per the mask) are automatically forced HIGH.
     *        The write is verified by an immediate read-back (output pins
     *        only - input pins reflect real external levels, not what was
     *        written, so they're excluded from the comparison); a mismatch
     *        is treated like a failed attempt and retried.
     * @param ok if non-null, set to true only if the write reached the
     *           device and was verified; false on I2C error or mutex timeout
     */
    void writeAll(uint8_t value, bool *ok = nullptr);

    /**
     * @brief Writes the state of a single output pin
     *        If the state is unchanged from cache, the I2C transaction is skipped.
     * @param pin   Pin number (0-7)
     * @param state true = HIGH, false = LOW
     * @param ok    if non-null, set to true if the device confirmed the new
     *              state (written and verified, or skipped because the
     *              cache already matched); false on I2C error, mutex
     *              timeout, or invalid pin
     * @return true if the write was performed, false if skipped (state unchanged)
     */
    bool writePin(uint8_t pin, bool state, bool *ok = nullptr);

    /**
     * @brief Returns the last read/written port state (no I2C communication)
     */
    uint8_t getCache() const;

    /**
     * @brief Tests whether the device responds on the I2C bus. Also serves
     *        as the lazy retry point if the device was never (yet)
     *        registered — same as any other method (see begin()).
     * @return true if the device responds
     */
    bool isOnline();

    /**
     * @brief Node for the onReconnect()/onDisconnect() listener lists.
     *        The caller owns the instance (e.g. as a local/member variable
     *        alongside the AP_PCF8574 it's registered on) - no heap
     *        allocation happens anywhere in the listener mechanism, so
     *        there's no fixed cap on how many can be registered. The
     *        Listener must stay alive for as long as it's registered (it's
     *        linked into an intrusive list, not copied) - in practice this
     *        just means declaring it in the same scope that outlives the
     *        AP_PCF8574 instance, same as the instance itself.
     */
    class Listener
    {
    public:
        explicit Listener(std::function<void()> callback) : _callback(std::move(callback)) {}
        Listener(const Listener &) = delete;
        Listener &operator=(const Listener &) = delete;

    private:
        friend class AP_PCF8574;
        std::function<void()> _callback;
        Listener              *_next = nullptr;
    };

    /**
     * @brief Registers a listener fired when the device transitions from
     *        offline to online - either a delayed/retried registration
     *        finally succeeding, or a read/write recovering after
     *        failures. Fired after the internal mutex is released, so the
     *        callback may safely call other public methods on this
     *        instance.
     *
     *        Multiple listeners can be registered - each call adds one,
     *        none are ever replaced or removed. Meant for a single
     *        instance shared across independent tasks (e.g. one PCF8574
     *        driving both a servo valve and a separate pulse counter,
     *        each in their own task) - each task registers its own
     *        listener without stepping on the other's.
     *
     *        Typical use: re-apply pin mode / output state after a
     *        reconnect, since the chip forgets everything it was told
     *        across its own power cycle (power-on default is all pins
     *        HIGH) - a plain reconnect of the I2C bus/ESP doesn't need
     *        this (the chip kept its state), but there's no way to tell
     *        the two apart from here, so treat every reconnect as if the
     *        chip may have forgotten.
     */
    void addReconnectListener(Listener &listener);

    /**
     * @brief Registers a listener fired when the device transitions from
     *        online to offline (a read/write exhausted its retries).
     *        Fired after the internal mutex is released, same as
     *        addReconnectListener(). Multiple listeners can be registered,
     *        same as addReconnectListener().
     */
    void addDisconnectListener(Listener &listener);

private:
    esp_err_t _write(uint8_t data);
    esp_err_t _read(uint8_t &data);

    // Forces input pins HIGH in a port value about to be written.
    uint8_t _withInputsHigh(uint8_t value) const;

    // Registration attempt, assuming _dev == nullptr - caller must hold
    // _mutex. Updates _online via _write()'s own bookkeeping.
    esp_err_t _tryBegin();

    // Lazy, rate-limited begin() retry - caller must hold _mutex. No-op
    // (and cheap) if already registered.
    bool _ensureBegun();

    // Fires all registered listeners for the given transition - caller
    // must NOT hold _mutex (a callback may call back into this instance).
    // Snapshots the current list head under the mutex first, then walks it
    // unlocked - safe because listeners are only ever prepended, never
    // removed or mutated once linked, so a concurrent addReconnectListener/
    // addDisconnectListener from another task can only add a newer head,
    // never disturb the (immutable) chain this snapshot already points into.
    void _fireReconnect();
    void _fireDisconnect();

    i2c_master_bus_handle_t _bus;
    uint8_t                 _address;
    uint32_t                _scl_hz;
    i2c_master_dev_handle_t _dev;
    uint8_t                 _data;       // cache of last port state
    uint8_t                 _inputMask;  // 1=input, 0=output
    uint32_t                _consecutiveFailures = 0;  // for log rate-limiting - reset on any success
    bool                    _online              = false;  // last known online/offline, for onReconnect/onDisconnect edge detection
    int64_t                 _lastBeginAttemptUs  = 0;      // for rate-limiting the lazy begin() retry

    Listener *_reconnectListeners  = nullptr;  // intrusive singly-linked list, head - see Listener
    Listener *_disconnectListeners = nullptr;

    SemaphoreHandle_t       _mutex;
    StaticSemaphore_t       _mutexBuffer; // static storage for the mutex - no heap, cannot fail on OOM
    char                    _tag[16];    // log tag with address ("PCF8574@0x20")
};
