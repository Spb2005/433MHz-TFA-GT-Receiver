# TFA-GT-433MHz-Decoder

### 🔔 Update Notice

> **Update:** Support for the **TFA Dostmann 30.3133** sensor has been added.  
>  
> To avoid confusion with the existing TFA Manchester-encoded sensors (e.g. **TFA 30.3208**), this sensor is referred to as **`TFA30`** throughout the code and the README.  
>  
> Please note that the current implementation is **not optimal**: the **TFA30 decoder is very similar to the GT-WT-02 decoder**, but both are implemented separately and therefore duplicate a lot of functionality.  
>  
> This could be improved with a **larger code overhaul and refactoring**, but I currently do not plan to do this.

## Introduction
This project provides several sketches that can **receive and decode signals** from  

- **TFA Dostmann 30.3208.02**  
- **TFA Dostmann 30.3133**
- **Global Tronics GT-WT-02**  

weather sensors and publish their data to an **MQTT server**.  

I mainly use the full-featured sketch (`Esp8266_TFA_GT_TFA30_MQTT_DHT_EEPROM`) to send all temperature and humidity data to **Node-RED**, store it in an **InfluxDB**, and visualize it on the Node-Red dashboard.

### Why this project is different
Most 433 MHz decoder libraries decode signals **inside the interrupt service routine (ISR)**, which can block other tasks.  
This project uses a **ring buffer**: the ISR only stores pulse data, while decoding happens later in `loop()`.  
This keeps the ISR short and improves system stability.

## Hardware

### MCUs
All sketches are written for **ESP8266**, but they should also work with **Arduino AVR** or **ESP32** boards with minor modifications:

**AVR boards**
- Change `ICACHE_RAM_ATTR` ISR to standard `ISR` function  
- Replace `Serial.printf()` with `Serial.print()`  
- Replace direct `GPIO_REG_READ` with `digitalRead()`  

**ESP32**
- Use the ESP32 WiFi library instead of the ESP8266 one  

I have **not tested** the code on AVR or ESP32 yet. 
Other MCUs should also be compatible with minor adjustments.

### 433 MHz Receiver
Any standard 433 MHz receiver module should work.  
Make sure to connect the receiver to an interrupt-capable pin:
- Arduino Uno/Nano: **D2** or **D3**  

### Weather Sensors
**Tested sensors:**  
- TFA Dostmann 30.3208.02  
- TFA Dostmann 30.3133
- Global Tronics GT-WT-02  

**Other sensors may also work (not tested):**

Similar to **TFA (Manchester-encoded):**
- Ambient Weather F007TH Thermo-Hygrometer  
- Ambient Weather F012TH Indoor/Display Thermo-Hygrometer  
- SwitchDoc Labs F016TH  

> You may need to adjust the `TFA_TYPE` or `MANCHESTER_CLOCK` definitions.  

Similar to **TFA30 (OOK-encoded):**
- TFA-Pool-thermometer 30.3160  

Similar to **GT (OOK-encoded):**
- Other GT-WT-02 clones from Lidl (AURIO), Teknihall, etc.  

### Additional Local Sensor
- DHT11  
- DHT22  

## Required Libraries

Make sure the following libraries are installed in the Arduino IDE (via Library Manager or manually):

- **EEPROM** (from ESP8266 core)  
- **ESP8266WiFi.h** (from ESP8266 core)  
- **PubSubClient** by Nick O’Leary  
- **DHT sensor library** by Adafruit  

## Available Code Examples

Currently, there are eight sketches:

- **`Esp8266_GT`**  
  Only GT decoder  

- **`Esp8266_TFA`**  
  Only TFA decoder  

- **`Esp8266_TFA_30.3133`**  
  Only TFA 30.3133 decoder

- **`Esp8266_TFA_GT`**  
  Both decoders combined, one receiver  

- **`Esp8266_TFA_GT_MQTT`**  
  Adds WiFi + MQTT functionality  
  Data is published as JSON to topic `TFA433/data`, e.g.:  
  ```json
  {"ID":221,"Channel":1,"Temperature":23.1,"Humidity":54,"Battery":1,"Type":69}
  ```
- **`Esp8266_TFA_GT_MQTT_DHT`**  
  Adds DHT sensor support  

- **`Esp8266_TFA_GT_MQTT_DHT_EEPROM`**  
  Adds EEPROM support for storing offsets.  
  Offsets can be updated via MQTT commands to `TFA433/cmd`:  
  - Request current offsets:  
    ```json
    {"showOffsets":1} 
    ```	
	Response is published on `TFA433/msg`.  
	
  - Update an offset:  
    ```json
    {"channel":1,"type":"temp","offset":0.5}
	```	
	or
	```json
    {"channel":1,"type":"hum","offset":-3}
    ```	
	The ESP responds first with the old offsets, then with the updated values.
	```json
	{"phase":"show","Temp_Adjust":[0,0,0,0,0,0,0,0],"Hum_Adjust":[0,0,0,0,0,0,0,0]}
    ```
  Default offsets can be hardcoded in:  
  ```cpp
  float Default_Temp_Adjust[8] = { ... };
  float Default_Hum_Adjust[8]  = { ... };
  ```
  These defaults are loaded if the EEPROM_INIT_MARKER is changed.
  
  Additionally the esp send this when it reconnects:
  ```json
	{"phase":"reconnect"}
  ```
- **`Esp8266_TFA_GT_TFA3_MQTT_DHT_EEPROM`** 
  builts on the Esp8266_TFA_GT_MQTT_DHT_EEPROM code and integrates the TFA 30.3133 decoder
  
# Channel Setup

My setup uses **8 channels**:

- 6 × TFA (up to 8 supported)  
- 1 × DHT (up to 1 supported)  
- 1 × GT (up to 3 supported) 
- 1 x TFA30 (up to 3 supported) 

You can configure:

- The **DHT channel** via `DHT22Channel`  
- The **GT channel offset** via `GTCHANNEL_OFFSET`  
- The **TFA30 channel offset** via `TFA30ChannelOffset` 

# Pulse Collection

Pulses are captured using a change interrupt and stored in a ring buffer:

```cpp
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
```
 
This approach works like a software-based version of the ESP32 RMT peripheral.  
It would be possible to adapt the code to use the RMT buffer directly, but that would require significant changes.

If the buffer overflows, a flag is set and any additional data is lost.  
Every 250 ms, the buffer is copied and decoded.

**Advantage:** The ISR (Interrupt Service Routine) remains very short – it only stores pulses, without performing any decoding.

## TFA Decoder

The **TFA Dostmann 30.3208** uses a Manchester-encoded signal.

### Protocol References
- [pilight TFA protocol](https://manual.pilight.org/protocols/433.92/weather/tfa2017.html) (not the "TFA 30.3200" Protocol)
- [rtl_433 TFA protocol](https://github.com/merbanan/rtl_433/blob/master/src/devices/ambient_weather.c)

### Implementation Notes
- Manchester decoding logic is based on: [victornpb/manch_decode](https://github.com/victornpb/manch_decode)  
- The byte decoding was inspired by [d10i/TFA433](https://github.com/d10i/TFA433).  
  I adapted this library to run on ESP boards ([Spb2005/TFAReceiver](https://github.com/Spb2005/TFAReceiver/tree/main))  
  and further restructured and improved it in this repository (not a library anymore).

The **TFA Dostmann 30.3133** uses a simpler OOK (On-Off Keying) pulse-length encoding.

### Protocol References
- [rtl_433 TFA30 protocol](https://github.com/merbanan/rtl_433/blob/master/src/devices/tfa_pool_thermometer.c)

### Implementation Notes
- I analysed the protoll and later found out, that it is basically the same as the TFA-Pool-thermometer 30.3160.
- The decoding of pulses and bytes is very simmilar to that of the GT-WT-02 sensor

## GT Decoder

The **GT-WT-02** uses a simpler OOK (On-Off Keying) pulse-length encoding.

### Protocol References
- [pilight GT-WT-02 decoder](https://manual.pilight.org/protocols/433.92/weather/teknihall.html)  
- [rtl_433 GT-WT-02 decoder](https://github.com/merbanan/rtl_433/blob/master/src/devices/gt_wt_02.c)  

The decoding of pulses and bytes was implemented entirely from scratch for this project.

## Extending with Additional Decoders

It is possible to add your own custom decoders. 

**Recommendation:**
- Create a function (similar to `checkTFA()` or `checkGT()`).
- Call it inside `checkBuffer()`, which executes every 250 ms to process the ring buffer.
  
## Inspirations and References

This project was inspired by and builds upon the following:

### Protocols
- TFA: [pilight TFA protocol](https://manual.pilight.org/protocols/433.92/weather/tfa2017.html)  
- TFA: [rtl_433 TFA protocol](https://github.com/merbanan/rtl_433/blob/master/src/devices/ambient_weather.c)  
- TFA30:[rtl_433 TFA30 protocol](https://github.com/merbanan/rtl_433/blob/master/src/devices/tfa_pool_thermometer.c)  
- GT: [pilight GT-WT-02 decoder](https://manual.pilight.org/protocols/433.92/weather/teknihall.html)  
- GT: [rtl_433 GT-WT-02 decoder](https://github.com/merbanan/rtl_433/blob/master/src/devices/gt_wt_02.c)  

### Code
- Manchester decoding: [victornpb/manch_decode](https://github.com/victornpb/manch_decode)  
- TFA decoding: [d10i/TFA433](https://github.com/d10i/TFA433)  

## Conclusion

- ✅ Reliable decoding for TFA and GT sensors  
- ✅ MQTT + DHT + EEPROM support included  
- ⚠️ Not yet tested on ESP32/AVR  
- ⚠️ Currently only provided as Arduino sketches (not packaged as libraries)  
