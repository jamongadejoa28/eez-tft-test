#include <Arduino.h>
#include <Wire.h>
#include <lvgl/lvgl.h>
#include <MPU6050.h>

#include "LGFX_Config.hpp"

extern "C" {
    #include "ui/ui.h"
    #include "ui/screens.h"
}

// ---------- Pins (ESP32-C3 SuperMini) ----------
#define PIN_SDA   8
#define PIN_SCL   9
#define PIN_SHUT  0       // VL6180X GPIO0/CE — high = enable

// ---------- Display ----------
#define SCREEN_W  320
#define SCREEN_H  170

static LGFX        tft;
static uint16_t    lv_buf_a[SCREEN_W * 16];
static uint16_t    lv_buf_b[SCREEN_W * 16];

static void disp_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;
    tft.startWrite();
    tft.pushImage(area->x1, area->y1, w, h, (uint16_t *)px);
    tft.endWrite();
    lv_display_flush_ready(disp);
}

static uint32_t lv_tick_cb(void) { return millis(); }

// ---------- VL6180X (direct, no library) ----------
// 16-bit register access per datasheet section 4.
#define VL_ADDR  0x29

static void vl_w8(uint16_t reg, uint8_t v) {
    Wire.beginTransmission(VL_ADDR);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xff));
    Wire.write(v);
    Wire.endTransmission();
}

static uint8_t vl_r8(uint16_t reg) {
    Wire.beginTransmission(VL_ADDR);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xff));
    Wire.requestFrom((uint8_t)VL_ADDR, (uint8_t)1);
    
    return Wire.read();
}

// Mandatory init sequence per ST AN4545 §9 ("SR03 settings").
static void vl_init(void) {
    // Private (undocumented) registers — required for correct ranging.
    vl_w8(0x0207, 0x01); vl_w8(0x0208, 0x01); vl_w8(0x0096, 0x00);
    vl_w8(0x0097, 0xFD); vl_w8(0x00E3, 0x00); vl_w8(0x00E4, 0x04);
    vl_w8(0x00E5, 0x02); vl_w8(0x00E6, 0x01); vl_w8(0x00E7, 0x03);
    vl_w8(0x00F5, 0x02); vl_w8(0x00D9, 0x05); vl_w8(0x00DB, 0xCE);
    vl_w8(0x00DC, 0x03); vl_w8(0x00DD, 0xF8); vl_w8(0x009F, 0x00);
    vl_w8(0x00A3, 0x3C); vl_w8(0x00B7, 0x00); vl_w8(0x00BB, 0x3C);
    vl_w8(0x00B2, 0x09); vl_w8(0x00CA, 0x09); vl_w8(0x0198, 0x01);
    vl_w8(0x01B0, 0x17); vl_w8(0x01AD, 0x00); vl_w8(0x00FF, 0x05);
    vl_w8(0x0100, 0x05); vl_w8(0x0199, 0x05); vl_w8(0x01A6, 0x1B);
    vl_w8(0x01AC, 0x3E); vl_w8(0x01A7, 0x1F); vl_w8(0x0030, 0x00);

    // Public configuration.
    vl_w8(0x0011, 0x10);  // hold off interrupts until cleared
    vl_w8(0x010A, 0x30);  // averaging sample period
    vl_w8(0x003F, 0x46);  // light/dark gain
    vl_w8(0x0031, 0xFF);  // auto-VHV repeat rate
    vl_w8(0x0040, 0x63);  // ALS integration time = 100 ms
    vl_w8(0x002E, 0x01);  // one-shot temperature cal

    vl_w8(0x001B, 0x09);  // range inter-measurement period
    vl_w8(0x003E, 0x31);  // ALS inter-measurement period
    vl_w8(0x0014, 0x24);  // INT on new-sample-ready

    vl_w8(0x0016, 0x00);  // clear fresh_out_of_reset
}

// Single-shot range; returns mm (0..255). 255 = out-of-range or timeout.
static uint8_t vl_range_mm(void) {
    vl_w8(0x0018, 0x01);                          // SYSRANGE__START = 1
    uint32_t t0 = millis();
    while ((vl_r8(0x004F) & 0x07) != 0x04) {     // wait "new sample ready"
        if (millis() - t0 > 50) return 255;
    }
    uint8_t mm = vl_r8(0x0062);                   // RESULT__RANGE_VAL
    vl_w8(0x0015, 0x07);                    // clear interrupts
    t0 = millis();      
    return mm;
}

static uint8_t errorMessage(){
    return (vl_r8(0x04D) >> 4);
}

// ---------- MPU6050 ----------
static MPU6050 mpu;
static bool    mpu_ok = false;
static bool    vl_ok  = false;

// ---------- UI handles (from EEZ screen tree) ----------
static lv_obj_t *img_w;
static lv_obj_t *lbl_w;
#define IMG_BASE_X 100
#define IMG_BASE_Y 35

static int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

void setup() {
    Serial.begin(115200);
    delay(50);
    Serial.println("eez-test boot");

    // VL6180X: drive CE high immediately. Datasheet §2.3: chip needs ≤ 1.4 ms after CE
    // rising edge to reach software-standby. We give it well more than that below.
    pinMode(PIN_SHUT, OUTPUT);
    digitalWrite(PIN_SHUT, LOW);
    delay(10);
    digitalWrite(PIN_SHUT , HIGH);
    delay(10);

    // Display first so we can show status.
    tft.init();
    tft.setRotation(1);
    tft.setSwapBytes(true);
    tft.fillScreen(TFT_BLACK);

    // LVGL.
    lv_init();
    lv_tick_set_cb(lv_tick_cb);
    lv_display_t *d = lv_display_create(SCREEN_W, SCREEN_H);
    lv_display_set_flush_cb(d, disp_flush_cb);
    lv_display_set_buffers(d, lv_buf_a, lv_buf_b, sizeof(lv_buf_a),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    ui_init();

    delay(10);
    // Grab EEZ widgets (child order on screen `main`: hello-label, image, text-label).
    img_w = lv_obj_get_child(objects.main, 1);
    lbl_w = lv_obj_get_child(objects.main, 2);
    lv_obj_align(lbl_w, LV_ALIGN_TOP_RIGHT, -4, 2);   // keep long text on-screen

    // I²C.
    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.setClock(400000);

    // MPU6050.
    mpu.initialize();
    mpu_ok = mpu.testConnection();
    if (mpu_ok) {
        mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_2);
        mpu.setDLPFMode(MPU6050_DLPF_BW_10);   // 10 Hz HW low-pass → smooth tilt
    }
    Serial.printf("MPU6050: %s\n", mpu_ok ? "OK" : "FAIL");

    delay(5);

    // VL6180X — probe, then init if present.
    Wire.beginTransmission(VL_ADDR);
    vl_ok = (Wire.endTransmission() == 0);
    for (byte address = 1; address < 127; address++){
        Wire.beginTransmission(address);
        if(Wire.endTransmission() == 0){
            Serial.print(address);
        }
    }
    if (vl_ok) {
        vl_init();
        Serial.println("VL6180X: OK");
    } else {
        Serial.println("VL6180X: no ACK at 0x29");
    }
}

void loop() {
    static uint32_t t_mpu = 0, t_vl = 0;
    static float    axf = 0, ayf = 0;
    uint32_t        now = millis();

    // Tilt → image position. 60 Hz keeps motion smooth.
    if (mpu_ok && now - t_mpu >= 16) {
        t_mpu = now;
        int16_t ax, ay, az, gx, gy, gz;
        mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

        // Light EMA on top of the hardware DLPF.
        axf = 0.4f * ax + 0.6f * axf;
        ayf = 0.4f * ay + 0.6f * ayf;

        int dx = clampi((int)(axf * 80.0f / 16384.0f), -80, 80);
        int dy = clampi((int)(ayf * 50.0f / 16384.0f), -50, 50);
        lv_obj_set_pos(img_w, IMG_BASE_X + dx, IMG_BASE_Y + dy);
    }

    // Distance → label. 5 Hz; one VL6180X measurement is ~10–30 ms.
    if (now - t_vl >= 200) {
        t_vl = now;
        char buf[16];
        if (!vl_ok) {
            snprintf(buf, sizeof(buf), "no dev");
            // Serial.printf("error: \n", errorMessage);
            Serial.println(VL_ADDR);
        } else {
            uint8_t mm = vl_range_mm();
            if (mm == 255) snprintf(buf, sizeof(buf), "--");
            else           snprintf(buf, sizeof(buf), "%u mm", mm);
        }
        lv_label_set_text(lbl_w, buf);
    }

    lv_timer_handler();
    delay(2);
}
