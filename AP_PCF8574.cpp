#include "AP_PCF8574.h"

AP_PCF8574::AP_PCF8574(i2c_master_bus_handle_t bus, uint8_t address)
    : _data(0xFF)
{
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = address;
    dev_cfg.scl_speed_hz = 100000;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &_dev));
}

uint8_t AP_PCF8574::readAll()
{
    i2c_master_receive(_dev, &_data, 1, 100);
    return _data;
}

bool AP_PCF8574::readPin(uint8_t pin, bool fromCache)
{
    if (!fromCache) {
        readAll();
    }
    return (_data >> pin) & 0x01;
}

void AP_PCF8574::writeAll(uint8_t value)
{
    _data = value;
    i2c_master_transmit(_dev, &_data, 1, 100);
}

void AP_PCF8574::writePin(uint8_t pin, bool state)
{
    readAll();
    if (state) {
        _data |= (1 << pin);
    } else {
        _data &= ~(1 << pin);
    }
    writeAll(_data);
}

uint8_t AP_PCF8574::getCache()
{
    return _data;
}
