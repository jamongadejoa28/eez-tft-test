# eez-tft-test

ESP32-C3 SuperMini + ST7789 (1.9", 170×320) TFT + MPU6050 + VL6180X.
EEZ Studio가 생성한 LVGL UI를 띄우고, MPU6050의 기울기에 따라 이미지를
움직이며, VL6180X로 측정한 거리를 라벨에 표시합니다.

## 하드웨어

| 모듈 | ESP32-C3 핀 |
|---|---|
| TFT MOSI (SDA) | GPIO6 |
| TFT SCLK (SCL/MSCK) | GPIO4 |
| TFT DC  (RS) | GPIO2 |
| TFT CS  (MCS) | GPIO7 |
| TFT RST | VDD (3.3 V) |
| MPU6050 SDA | GPIO8 |
| MPU6050 SCL | GPIO9 |
| VL6180X SDA | GPIO8 (공유 버스) |
| VL6180X SCL | GPIO9 (공유 버스) |
| VL6180X SHUT (GPIO0/CE) | GPIO0 |
| 모든 모듈 VDD | 3.3 V |

## 빌드 / 플래시

```bash
# 처음 한 번: LVGL 소스 다운로드 (lib/lvgl/는 .gitignore에 있음)
git clone --depth 1 --branch master https://github.com/lvgl/lvgl.git lib/lvgl

# 빌드
pio run

# 플래시 + 시리얼
pio run -t upload
pio device monitor
```

## 디렉토리 구조

```
include/lv_conf.h        # LVGL 설정
lib/lvgl/                # LVGL 소스 (gitignore — 위 명령으로 받기)
src/main.cpp             # 메인 코드
src/LGFX_Config.hpp      # LovyanGFX ST7789 설정
src/ui/                  # EEZ Studio 생성 UI 코드
docs/                    # 데이터시트 PDF
```
