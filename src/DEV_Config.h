/*****************************************************************************
* | File      	:   DEV_Config.h
* | Author      :   Waveshare team
* | Function    :   Hardware underlying interface
* | Info        :
*----------------
* |	This version:   V1.0
* | Date        :   2020-02-19
* | Info        :
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documnetation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to  whom the Software is
# furished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS OR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#
******************************************************************************/
#ifndef _DEV_CONFIG_H_
#define _DEV_CONFIG_H_

#include <Arduino.h>
#include <stdint.h>
#include <stdio.h>

/**
 * data
**/
#define UBYTE   uint8_t
#define UWORD   uint16_t
#define UDOUBLE uint32_t

/**
 * GPIO config
**/
#if defined(BOARD_TRMNL) || defined (BOARD_TRMNL_4CLR)
// Xiao ESP32C3 plus 8-pin breakout
//   #define EPD_SCK_PIN  8
//   #define EPD_MOSI_PIN 10
//   #define EPD_CS_PIN   21
//   #define EPD_RST_PIN  5
//   #define EPD_DC_PIN   4
//   #define EPD_BUSY_PIN 20
// Pin def for Xiao ESP32C3 + old EPD breakout
//   #define EPD_SCK_PIN  8
//   #define EPD_MOSI_PIN 10
//   #define EPD_CS_PIN   3
//   #define EPD_RST_PIN  2
//   #define EPD_DC_PIN   5
//   #define EPD_BUSY_PIN 7
   // Pin definition for TRMNL Board
  #define EPD_SCK_PIN  7
  #define EPD_MOSI_PIN 8
  #define EPD_CS_PIN   6
  #define EPD_RST_PIN  10
  #define EPD_DC_PIN   5
  #define EPD_BUSY_PIN 4
  #define SENSOR_SDA 21
  #define SENSOR_SCL 20
#elif defined(BOARD_XTEINK_X4)
  #define EPD_SCK_PIN  8
  #define EPD_MOSI_PIN 10
  #define EPD_CS_PIN   21
  #define EPD_RST_PIN  5
  #define EPD_DC_PIN   4
  #define EPD_BUSY_PIN 6

#elif defined(BOARD_WAVESHARE_ESP32_DRIVER)
   // Pin definition for Waveshare ESP32 Driver Board
   #define EPD_SCK_PIN  13
   #define EPD_MOSI_PIN 14
   #define EPD_CS_PIN   15
   #define EPD_RST_PIN  26
   #define EPD_DC_PIN   27
   #define EPD_BUSY_PIN 25

#elif defined(BOARD_WAVESHARE_397)
   #define EPD_SCK_PIN  11
   #define EPD_MOSI_PIN 12
   #define EPD_CS_PIN   10
   #define EPD_RST_PIN  46
   #define EPD_DC_PIN   9
   #define EPD_BUSY_PIN 3
#define FAKE_BATTERY_VOLTAGE
#elif defined(BOARD_SEEED_XIAO_ESP32C3)
   // Pin definition for Seeed XIAO ESP32C3 Board
   #define EPD_SCK_PIN  8
   #define EPD_MOSI_PIN 10
   #define EPD_CS_PIN   3
   #define EPD_RST_PIN  2
   #define EPD_DC_PIN   5
   #define EPD_BUSY_PIN 4

#elif defined(BOARD_SEEED_XIAO_ESP32S3)
   // Pin definition for Seeed XIAO ESP32S3 Board
   #define EPD_SCK_PIN  7
   #define EPD_MOSI_PIN 9
   #define EPD_CS_PIN   2
   #define EPD_RST_PIN  1
   #define EPD_DC_PIN   4
   #define EPD_BUSY_PIN 3
   
#elif (defined(BOARD_XIAO_EPAPER_DISPLAY) || defined(BOARD_XIAO_EPAPER_DISPLAY_3CLR))
   // Pin definition for TRMNL 7inch5 OG DIY Kit
   #define EPD_SCK_PIN  7
   #define EPD_MOSI_PIN 9
   #define EPD_CS_PIN   44
   #define EPD_RST_PIN  38
   #define EPD_DC_PIN   10
   #define EPD_BUSY_PIN 4
   // DEBUG - remove the fake battery line after testing
   #define FAKE_BATTERY_VOLTAGE
#elif defined(BOARD_TRMNL_X)
   #define FAKE_BATTERY_VOLTAGE

#elif defined(BOARD_SEEED_RETERMINAL_E1001) || defined(BOARD_SEEED_RETERMINAL_E1002)
   // Pin definition for reTerminal E1001 & E1002
   #define EPD_SCK_PIN  7
   #define EPD_MOSI_PIN 9
   #define EPD_CS_PIN   10
   #define EPD_RST_PIN  12
   #define EPD_DC_PIN   11
   #define EPD_BUSY_PIN 13
#elif defined(BOARD_XIAO_ESP32C6_75V1)
   // Pin definition for Seeed XIAO ESP32-C6 + Waveshare 7.5" V1 (SKU 13187, GDEW075T8)
   // Panel driven by GxEPD2_750 on the C6's default SPI bus.
   #define EPD_SCK_PIN  19    // D8  — default SPI SCK
   #define EPD_MOSI_PIN 18    // D10 — default SPI MOSI
   #define EPD_CS_PIN   21    // D3
   #define EPD_DC_PIN   16    // D6
   #define EPD_RST_PIN  17    // D7
   #define EPD_BUSY_PIN  2    // D2
#elif defined(BOARD_WAVESHARE_1248B)
   // Pin definition for Waveshare ESP32 Driver Board (SKU 15823, WROVER variant)
   // + 12.48" e-Paper Module B (SKU 17299). Four-controller panel driven via
   // GxEPD2's GxEPD2_1248c class on hardware SPI.
   //
   // Reference: Waveshare's "12.48inch-e-paper" sample repo
   //   (esp32/esp32-epd-12in48/src/DEV_Config.h on GitHub:
   //    https://github.com/waveshareteam/12.48inch-e-paper)

   // Shared SPI bus
   #define EPD_SCK_PIN        13
   #define EPD_MOSI_PIN       14

   // Per-quadrant CS
   #define EPD_M1_CS_PIN      23   // top-left
   #define EPD_S1_CS_PIN      22   // top-right
   #define EPD_M2_CS_PIN      16   // bottom-left
   #define EPD_S2_CS_PIN      19   // bottom-right

   // DC shared per half
   #define EPD_M1S1_DC_PIN    25   // top half data/command select
   #define EPD_M2S2_DC_PIN    17   // bottom half data/command select

   // RST shared per half
   #define EPD_M1S1_RST_PIN   33   // top half reset
   #define EPD_M2S2_RST_PIN    5   // bottom half reset

   // Per-quadrant BUSY
   #define EPD_M1_BUSY_PIN    32
   #define EPD_S1_BUSY_PIN    26
   #define EPD_M2_BUSY_PIN    18
   #define EPD_S2_BUSY_PIN     4
#elif defined (BOARD_X_CLASS)
// Parallel Eink devices don't explicitly define GPIO pins for the display here
#else
   #error "Board type not defined. Please define BOARD_WAVESHARE_ESP32_DRIVER or BOARD_TRMNL or BOARD_SEEED_XIAO_ESP32C3 or BOARD_SEEED_XIAO_ESP32S3 in platformio.ini build_flags."
#endif

#define GPIO_PIN_SET   1

/**
 * GPIO read and write
**/
#define DEV_Digital_Write(_pin, _value) digitalWrite(_pin, _value == 0? LOW:HIGH)
#define DEV_Digital_Read(_pin) digitalRead(_pin)

/**
 * delay x ms
**/
#define DEV_Delay_ms(__xms) delay(__xms)

/*------------------------------------------------------------------------------------------------------*/
UBYTE DEV_Module_Init(void);
void DEV_SPI_WriteByte(UBYTE data);

#endif
