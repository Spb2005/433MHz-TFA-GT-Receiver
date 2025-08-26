# TFA-GT-433MHz-Decoder

## Introduction
- What this project does (receives and decodes signals from TFA Dostmann 30.3208.02 and Global Tronics GT-WT-02 weather stations)
- Why it might be useful (integration into ESP8266/ESP32, MQTT, home automation, etc.)

## Hardware
###MCUs
All codes where written for Esp8266, it should work with Esp32 and Arduino AVR boards.
You may need to do some changes:

AVR:
- changing ISR to standart function
- changing oder delting Serial.printf() functions

Esp32:
- changing wifi library to Esp32 wifi

I have not tested it on Arduino Avr or Esp32.
Other MCUs should be compatible with other changes.

###Receiver
Any standard 433 MHz receiver module should work.

Make sure to connect the receiver to an interrupt-capable pin:

* On Arduino Uno/Nano: use **D2** or **D3**

###Temperature sensors
- TFA Dostmann 30.3208.02
- Global Tronics GT-WT-02

These sensors may also work (not tested):
similar protocoll to TFA:
* Ambient Weather F007TH Thermo-Hygrometer
* Ambient Weather F012TH Indoor/Display Thermo-Hygrometer
* SwitchDoc Labs F016TH

similar protocoll to GT:
* other GT-WT-02 sensors from different manufactures like Lidl AURIO or Teknihall 

### additional local sensor

* DHT11 
* DHT22

## Code 
currently thera are 6 Codes available:

* Esp8266_GT
* Esp8266_TFA
* Esp8266_TFA_GT
* Esp8266_TFA_GT_MQTT
* Esp8266_TFA_GT_MQTT_DHT
* Esp8266_TFA_GT_MQTT_DHT_EEPROM

Esp8266_GT and Esp8266_TFA
contain only the decoding algorithm for each sensor

Esp8266_TFA_GT
contains both sensors in one code and uses one receiver

Esp8266_TFA_GT_MQTT
adds wifi + mqtt (sending) functionality

Esp8266_TFA_GT_MQTT_DHT
adds DHT 22 support

Esp8266_TFA_GT_MQTT_DHT_EEPROM
adds EEPROM to store offset variables, which canbe changed with mqtt commands:

publish "{"showOffsets":1}" to TFA433/cmd to receive current offset values on TFA433/msg
publish "{"channel": 1,"type": "temp","offset": 0}" or "{"channel": 1,"type": "hum","offset": 0}" to TFA433/cmd to change the offset variables.
The Esp answers on TFA433/msg to first send the old offsets followed by the new offsets

it is possible to hard-code default values for the offsets, which are applied, if the EEPROM_INIT_MARKER is changed
change these arrays:

float Default_Temp_Adjust[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
float Default_Hum_Adjust[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

## Pulse Collection
- Explanation of how raw 433 MHz pulses are captured using interrupts  
- Description of the ring buffer (`PulseRingBuffer`)  
- Notes on buffer size and possible overflows  

## TFA Decoder
- Explanation of Manchester decoding  
- Data structure of TFA messages  
- How checksum validation is performed  
- Example output  

## GT Decoder
- Explanation of pulse-train decoding  
- How bits are reconstructed from pulse durations  
- Checksum logic and data format  
- Example output  

## Inspirations and References
- Mention projects, blogs, or repositories that inspired you  
- Link to datasheets or protocol reverse engineering notes (if available)  
- Acknowledge code snippets you adapted  

## Conclusion
- Summary of what works  
- Known limitations  
- Possible future improvements (turning into a proper Arduino library, supporting more sensors, etc.)  
