#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// ST7789 wiring on ESP32-C3 SuperMini:
//   MOSI = GPIO6  (SDA on the TFT)
//   SCLK = GPIO4  (SCL on the TFT, labeled "MSCK" in the project notes)
//   DC   = GPIO2  (RS on the TFT)
//   CS   = GPIO7  (MCS on the TFT)
//   RST  = tied to VDD (no GPIO)
//   BLK  = tied to GND/VDD externally
class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 _panel_instance;
    lgfx::Bus_SPI       _bus_instance;

public:
    LGFX(void) {
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host   = SPI2_HOST;
            cfg.spi_mode   = 0;
            cfg.freq_write = 40000000;
            cfg.freq_read  = 16000000;
            cfg.spi_3wire  = false;
            cfg.use_lock   = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk   = 4;
            cfg.pin_mosi   = 6;
            cfg.pin_miso   = -1;
            cfg.pin_dc     = 2;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }
        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs           = 7;
            cfg.pin_rst          = -1;   // tied to VDD
            cfg.pin_busy         = -1;
            cfg.panel_width      = 170;
            cfg.panel_height     = 320;
            cfg.offset_x         = 35;   // 170-wide ST7789 variants need an x-offset
            cfg.offset_y         = 0;
            cfg.offset_rotation  = 0;    // offsets are given in native (portrait) orientation
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits  = 1;
            cfg.readable         = false;
            cfg.invert           = true;
            cfg.rgb_order        = false;
            cfg.dlen_16bit       = false;
            cfg.bus_shared       = false;
            _panel_instance.config(cfg);
        }
        setPanel(&_panel_instance);
    }
};
