# TFA-GT-433MHz-Decoder

## Introduction
This project provides several sketches that can **receive and decode signals** from  
- **TFA Dostmann 30.3208.02**  
- **Global Tronics GT-WT-02**  

weather sensors and publish their data to an **MQTT server**.  

I mainly use the full-featured sketch (`Esp8266_TFA_GT_MQTT_DHT_EEPROM`) to send all temperature and humidity data to **Node-RED**, store it in an **InfluxDB**, and visualize it on the Node-Red dashboard.

### Why this project is different
Most 433 MHz decoder libraries decode signals **inside the interrupt service routine (ISR)**, which can block other tasks.  
This project uses a **ring buffer**: the ISR only stores pulse data, while decoding happens later in `loop()`.  
This keeps the ISR short and improves system stability.

---

## Hardware

### MCUs
All sketches are written for **ESP8266**, but they should also work with **ESP32** or **Arduino AVR** boards with minor modifications:

**AVR boards**
- Change `ICACHE_RAM_ATTR` ISR to standard `ISR` function  
- Replace `Serial.printf()` with `Serial.print()`  
- Replace direct `GPIO_REG_READ` with `digitalRead()`  

**ESP32**
- Use the ESP32 WiFi library instead of the ESP8266 one  

I have **not tested** the code on AVR or ESP32 yet. Other MCUs should also be compatible with minor adjustments.

### 433 MHz Receiver
Any standard 433 MHz receiver module should work.  
Make sure to connect the receiver to an interrupt-capable pin:
- Arduino Uno/Nano: **D2** or **D3**  

### Weather Sensors
Tested sensors:  
- TFA Dostmann 30.3208.02  
- Global Tronics GT-WT-02  

Other sensors may also work (not tested):  

**Similar to TFA (Manchester-encoded):**  
- Ambient Weather F007TH Thermo-Hygrometer  
- Ambient Weather F012TH Indoor/Display Thermo-Hygrometer  
- SwitchDoc Labs F016TH  

> You may need to adjust the `TFA_TYPE` or `MANCHESTER_CLOCK` definitions.  

**Similar to GT (OOK-encoded):**  
- Other GT-WT-02 clones from Lidl (AURIO), Teknihall, etc.  

### Additional Local Sensor
- DHT11  
- DHT22  

---

## Available Code Examples
Currently, there are six sketches:

- `Esp8266_GT`  
  Only GT decoder  

- `Esp8266_TFA`  
  Only TFA decoder  

- `Esp8266_TFA_GT`  
  Both decoders combined, one receiver  

- `Esp8266_TFA_GT_MQTT`  
  Adds WiFi + MQTT functionality  
  Data is published as JSON to topic `TFA433/data`, e.g.:  
  ```json
  {"ID":221,"Channel":1,"Temperature":23.1,"Humidity":54,"Battery":1,"Type":69}
  
- `Esp8266_TFA_GT_MQTT_DHT`  
  Adds DHT support

- `Esp8266_TFA_GT_MQTT_DHT_EEPROM`  
  Adds EEPROM support to store offsets.
  Offsets can be updated via MQTT commands to TFA433/cmd:
  * Request current offsets:
    {"showOffsets":1}
    Response on TFA433/msg
  * Update an offset:
    {"channel":1,"type":"temp","offset":0.5}
    {"channel":1,"type":"hum","offset":-3}
    The Esp answers first with the old offsets followed by the new offsets.
  
  Default offsets can be hardcoded in:
  float Default_Temp_Adjust[8] = { ... };
  float Default_Hum_Adjust[8] = { ... };
  These are loaded, if the EEPROM_INIT_MARKER is changed

## Channel Setup

My setup uses 8 channels:

* 6 × TFA (max 8)
* 1 × DHT (max 1)
* 1 × GT (max 3)

You can adjust:

* DHT channel in readDHT()
* GT channel offset via CHANNEL_OFFSET

## Pulse Collection

Pulses are captured via a change interrupt and stored in a ring buffer:

struct PulsePair {
  uint32_t time;
  bool level;
};

struct PulseRingBuffer {
  PulsePair buffer[BUFFER_SIZE];
  volatile uint16_t writeIndex = 0;
  volatile uint16_t readIndex = 0;
  volatile uint16_t count = 0;
};

This works like a software version of the ESP32 RMT peripheral.
You could probably change to code to use the RMT buffer, but this a bigger change to the code.

If the buffer overflows, a flag is set and all additional data is lost.
Every 250 ms the buffer is copied and decoded.

Advantage: The ISR is very short – only storing pulses, no decoding.

## TFA Decoder

The TFA Dostmann 30.3208 uses a Manchester-encoded signal.

### Protocol references
  * https://manual.pilight.org/protocols/433.92/weather/tfa2017.html
  * https://github.com/merbanan/rtl_433/blob/master/src/devices/ambient_weather.c

### Implementation notes
  * Manchester decoding logic is based on:
  * https://github.com/victornpb/manch_decode
  The decogin off the Bytes is inspired by this library: https://github.com/d10i/TFA433 which i firstly improved to be able to run on ESP boards: https://github.com/Spb2005/TFAReceiver/tree/main and now further improved in this repo

## GT Decoder

The GT-WT-02 uses a simpler OOK pulse-length encoding.

### Protocol references
  * https://manual.pilight.org/protocols/433.92/weather/teknihall.html
  * https://github.com/merbanan/rtl_433/blob/master/src/devices/gt_wt_02.c
  The decoding of pulses and bytes was written from scratch for this project.

## Extending with Additional Decoders

You can add custom decoders.
Recommendation:
  * Create a function (similar to checkTFA() or checkGT())
  * Call it inside checkBuffer(), which runs every 250 ms and processes the ring buffer
  
## Inspirations and References

This project was inspired by and builds upon
### Protocols:

  * TFA: https://manual.pilight.org/protocols/433.92/weather/tfa2017.html
  * TFA: https://github.com/merbanan/rtl_433/blob/master/src/devices/ambient_weather.c
  * GT: https://manual.pilight.org/protocols/433.92/weather/teknihall.html
  * GT: https://github.com/merbanan/rtl_433/blob/master/src/devices/gt_wt_02.c

### Code:

  * Manchester decoding https://github.com/victornpb/manch_decode
  * TFA Decoding https://github.com/d10i/TFA433
  
## Conclusion

  * ✅ Decoding for TFA and GT sensors is working reliably
  * ✅ MQTT + DHT + EEPROM support included
  * ⚠️ Not yet tested on ESP32/AVR
  * ⚠️ Currently only provided as sketches (not Arduino libraries) 
