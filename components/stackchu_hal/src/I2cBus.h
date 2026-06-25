// I2cBus — 共用 I2C マスター補助（内部用）
// AXP2101 / BMI270 は同じ内部 I2C バスを共有するため、
// 1 箇所でバスを初期化してハンドルを共有する。
#pragma once

#include "HalPins.h"
#include "driver/i2c.h"
#include "esp_err.h"

namespace stackchu {
namespace i2cbus {

// 共用バスを初期化。既に初期化済みなら何もしない（成功扱い）。
inline esp_err_t ensureInit() {
    static bool inited = false;
    if (inited) return ESP_OK;

    i2c_config_t cfg = {};
    cfg.mode = I2C_MODE_MASTER;
    cfg.sda_io_num = pins::SDA;
    cfg.scl_io_num = pins::SCL;
    cfg.sda_pullup_en = GPIO_PULLUP_ENABLE;
    cfg.scl_pullup_en = GPIO_PULLUP_ENABLE;
    cfg.master.clk_speed = pins::FREQ_HZ;
#if ESP_IDF_VERSION_MAJOR >= 5
    cfg.clk_flags = 0;  // デフォルト
#endif
    esp_err_t err = i2c_param_config(static_cast<i2c_port_t>(pins::I2C_NUM), &cfg);
    if (err != ESP_OK) return err;
    err = i2c_driver_install(static_cast<i2c_port_t>(pins::I2C_NUM),
                             I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_ERR_INVALID_STATE && err != ESP_OK) return err;
    inited = true;
    return ESP_OK;
}

inline i2c_port_t port() { return static_cast<i2c_port_t>(pins::I2C_NUM); }

// レジスタ読出し（1 バイト）。
inline esp_err_t readReg8(uint8_t addr, uint8_t reg, uint8_t* out) {
    return i2c_master_write_read_device(port(), addr, &reg, 1, out, 1,
                                       pdMS_TO_TICKS(100));
}

// レジスタ書込み（1 バイト）。
inline esp_err_t writeReg8(uint8_t addr, uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    return i2c_master_write_to_device(port(), addr, buf, 2, pdMS_TO_TICKS(100));
}

// 連続読出し。
inline esp_err_t readBurst(uint8_t addr, uint8_t reg, uint8_t* dst, size_t n) {
    return i2c_master_write_read_device(port(), addr, &reg, 1, dst, n,
                                       pdMS_TO_TICKS(200));
}

}  // namespace i2cbus
}  // namespace stackchu
