#pragma once

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/**
 * @brief Driver pro PCF8574 / PCF8574A — 8-bitový I2C GPIO expandér
 *
 * Thread-safe, retry logika pro přechodné I2C chyby, vstupní piny
 * jsou trvale drženy HIGH (quasi-bidirectional I/O PCF8574).
 */
class AP_PCF8574
{
public:
    /**
     * @brief Konstruktor
     * @param bus     I2C master bus handle (z i2c_new_master_bus)
     * @param address I2C adresa (0x20-0x27 pro PCF8574, 0x38-0x3F pro PCF8574A)
     * @param scl_hz  Rychlost SCL v Hz (výchozí 100 kHz)
     */
    AP_PCF8574(i2c_master_bus_handle_t bus, uint8_t address, uint32_t scl_hz = 100000);
    ~AP_PCF8574();

    AP_PCF8574(const AP_PCF8574 &) = delete;
    AP_PCF8574 &operator=(const AP_PCF8574 &) = delete;

    /**
     * @brief Nastaví směr pinů maskou (1=input, 0=output)
     *        Vstupní piny se při každém write automaticky drží na HIGH.
     *        Výchozí: 0xFF (všechny input)
     */
    void setPinMode(uint8_t mask);

    /**
     * @brief Nastaví směr jednoho pinu
     * @param pin   Číslo pinu (0-7)
     * @param input true = input, false = output
     */
    void setPinMode(uint8_t pin, bool input);

    /**
     * @brief Přečte všech 8 pinů najednou přes I2C
     * @return 8bit hodnota portu (0xFF při I2C chybě — fail-safe)
     */
    uint8_t readAll();

    /**
     * @brief Přečte stav jednoho pinu
     * @param pin       Číslo pinu (0-7)
     * @param fromCache true = vrátí poslední načtený stav bez I2C komunikace
     * @return true = HIGH, false = LOW
     */
    bool readPin(uint8_t pin, bool fromCache = false);

    /**
     * @brief Zapíše všech 8 pinů najednou
     *        Vstupní piny (dle masky) se automaticky nastaví na HIGH.
     */
    void writeAll(uint8_t value);

    /**
     * @brief Zapíše stav jednoho výstupního pinu
     *        Pokud se stav oproti cache nezměnil, přeskočí I2C transakci.
     * @param pin   Číslo pinu (0-7)
     * @param state true = HIGH, false = LOW
     * @return true pokud byl zápis proveden, false pokud byl přeskočen (stav se nezměnil)
     */
    bool writePin(uint8_t pin, bool state);

    /**
     * @brief Vrátí poslední načtený/zapsaný stav portu (bez I2C komunikace)
     */
    uint8_t getCache() const;

    /**
     * @brief Testuje dostupnost zařízení na I2C busu
     * @return true pokud zařízení odpovídá
     */
    bool isOnline();

private:
    void      _write(uint8_t data);
    bool      _read(uint8_t &data);

    i2c_master_dev_handle_t _dev;
    uint8_t                 _data;       // cache posledního stavu
    uint8_t                 _inputMask;  // 1=input, 0=output
    SemaphoreHandle_t       _mutex;
    char                    _tag[16];    // log tag s adresou ("PCF8574@0x20")
};
