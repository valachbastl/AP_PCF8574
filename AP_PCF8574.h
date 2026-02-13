#pragma once

#include "driver/i2c_master.h"
#include "esp_log.h"

class AP_PCF8574
{
public:
    /**
     * @brief Konstruktor
     * @param bus I2C master bus handle (z i2c_new_master_bus)
     * @param address I2C adresa (0x20-0x27 pro PCF8574, 0x38-0x3F pro PCF8574A)
     */
    AP_PCF8574(i2c_master_bus_handle_t bus, uint8_t address);

    /**
     * @brief Precte vsech 8 pinu najednou
     * @return 8bit hodnota portu
     */
    uint8_t readAll();

    /**
     * @brief Precte stav jednoho pinu
     * @param pin Cislo pinu (0-7)
     * @param fromCache true = vrati posledni nacteny stav bez I2C komunikace
     * @return true = HIGH, false = LOW
     */
    bool readPin(uint8_t pin, bool fromCache = false);

    /**
     * @brief Zapise vsech 8 pinu najednou
     * @param value 8bit hodnota portu
     */
    void writeAll(uint8_t value);

    /**
     * @brief Zapise stav jednoho pinu
     * @param pin Cislo pinu (0-7)
     * @param state true = HIGH, false = LOW
     */
    void writePin(uint8_t pin, bool state);

    /**
     * @brief Vrati posledni nacteny/zapsany stav portu (bez I2C komunikace)
     * @return 8bit hodnota portu
     */
    uint8_t getCache();

private:
    i2c_master_dev_handle_t _dev;
    uint8_t _data;
};
