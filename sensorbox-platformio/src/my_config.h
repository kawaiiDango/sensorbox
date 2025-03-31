#pragma once

#define SUPPORTS_TOUCH
// #define THE_BOX
// #define ENABLE_LOW_BATTERY_SHUTDOWN
// #define PRINT_CBOR


#ifdef THE_BOX

#define PIR_PIN 36
#define TOUCH_PIN 4
#define SDS_POWER_PIN 25
#define SDS_TX_PIN 39
#define SDS_RX_PIN 17
#define LCD_RST_PIN 16
#define LCD_CS_PIN 12
#define LCD_DC_PIN 15
#define LCD_DIN_PIN 23
#define LCD_CLK_PIN 18
#define LCD_BACKLIGHT_PIN 2
#define LED_PIN 2
#define LCD_INVERSE false    // set to true to invert display pixel color
#define MY_LCD_CONTRAST 0xB1 // default is 0xBF set in LCDinit, Try 0xB1 <-> 0xBF if your display is too dark/dim
#define MY_LCD_BIAS 0x12     // LCD bias mode 1:48: Try 0x12 or 0x13 or 0x14
#define BUTTON_PIN 27
#define BATTERY_VOLTAGE_PIN 34
#define I2S_WS_PIN 13
#define I2S_SCK_PIN 14
#define I2S_SD_PIN 35
// The mic is powered from a gpio pin to allow it to be turned off when not in use
#define MIC_POWER_PIN 26
// write to its eeprom if the required settings are different from the stored ones
#define SCD41_WRITE_EEPROM_ENABLED false
#else
// #define VOC_PREHEAT_TIMEOUT (3 * 60 * 1000)
#define TOUCH_PIN 15
#define LED_PIN 5
#define BUTTON_PIN 39
#define BATTERY_VOLTAGE_PIN 36

#endif

#define DEBUG_BAUD_RATE 115200
