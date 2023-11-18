# sensorbox

I took a bunch of sensors and shoved them into a box.

## Hardware used

- Firebeetle 2 ESP32 board
- 3 x 18650 batteries in parallel and a holder for them
- TSL2591 ambient light sensor
- DHT20 temperature and humidity sensor
- BMP280 or HW-611 pressure sensor
- AGS01DB VOC sensor (to a different ESP32 not powered by battery because of high power requirements)
- SDS018 particulate matter sensor
- INMP441 microphone
- HC-SR501 PIR motion sensor
- MT3608 DC-DC step-up converter (To power the 5V PM sensor from the 3.7V battery)
- IRLZ44N MOSFET (To switch the MT3608 (and so the PM sensor) on and off)
- PCD8544 LCD display
- Resistors for the LCD backlight pin, and I2C pullups

## Wiring

Regex search for "#define .+\_PIN" in the project to find the pin assignments.

I2C devices are connected to the default I2C pins (21 and 22).

## Software

This is a PlatformIO project with the ESP-IDF framework with Arduino as a component,
so that I can mess with menuconfig and still use Arduino libraries.

The server consists of influxdb, grafana, a CoAP server, a grafana webhook to FCM bridge and a CoAP to FCM bridge.

The android app is a simple FCM client with a home screen widget.

## Working

### Configuration

On the first boot, the ESP32 will create a WiFi access point called "sensorBox" with the password "sensorBox321".

The captive portal runs on 192.168.4.1.

If the device is offline, it will take the time from your browser when you submit the form.

To change settings later, wake up the ESP32 from the touchpad and press the button on BUTTON_PIN.

To configure the server, check the details in sensorbox-server/consts.py.example

To configure the android app, download "google-services.json" into sensorbox-android/app after creating a firebase/FCM project.

Also check sensorbox-android/local.properties.example

### Data collection

The ESP32 wakes up to collect data from the sensors every 2 minutes (configurable),
and send it to the server every 30 mins (configurable).

To save power, the ESP32 goes into deep sleep in between and runs at 80MHz most of the time.

Since the PM sensor is power hungry even in sleep mode,
it is only powered on when it is needed and collects data every 3 x 2 minutes (configurable).

### Data transmission

The ESP32 sends the data to the server using CoAP non-confirmable messages.
It sends multiple messages at once and waits for custom defined ACKs in parallel.
It will retry sending those measurements which did not receive an ACK, later.

I have found that this is way faster and more reliable than using MQTT QOS 1 messages,
especiallty when the ping to my server is over 200ms.

I also noticed that using an open WiFi network instead of WPA2-PSK,
and using a static IP instead of DHCP,
decreases the connection time from 3 seconds to 0.2 seconds.

### Display

The ESP32 will display the current measurements on the LCD display when the touchpad is touched.

Touching while the display is on, will change the page.

The display will turn off after 20 seconds.

The android app can display the readings in a home screen widget. It receives the data from the server using FCM.

### Storage

The ESP32 stores the measurements in a ring buffer in RTC memory.

Every time the RTC buffer is full, and the ESP32 still cannot connect to the server,
it will start saving the measurements to the flash memory in a filesystem based ring buffer.

This way, it can store weeks worth of measurements, while completely offline,
as long as the time was set through the captive portal on the first boot.

On starting the configuration mode, the ESP32 will dump the current contents of th RTC buffer to the flash buffer immediately.
The configuration page also diaplays the number of measurements in the flash buffer, and provides a way to delete them.

The CoAP server receives the measurements from the ESP32 and pushes them into InfluxDB and FCM.

### Alerts

The server can send alerts to the android app using FCM using topics the user has subscribed to, from the app.

It currently uses topics for: the widget, daily digests and one forfor granafa alerts.

## Credits

- https://github.com/SpoturDeal/ESP32-dBmeter-with-MQTT and https://github.com/ikostoski/esp32-i2s-slm for the INMP441 driver
- https://github.com/adafruit/Adafruit_BMP280_Library
- https://github.com/DFRobot/DFRobot_DHT20
- https://github.com/adafruit/Adafruit_TSL2591_Library
- https://github.com/adafruit/Adafruit-PCD8544-Nokia-5110-LCD-library
- https://github.com/lewapek/sds-dust-sensors-arduino-library
- https://github.com/cdjq/DFRobot_AGS01DB
