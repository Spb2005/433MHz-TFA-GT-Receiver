# TFA-GT-433MHz-Decoder

## Introduction
This project contains several codes, which can decode TFA Dostmann 30.3208.02 and Global Tronics GT-WT-02 weather sensor and send their data to an mqtt server. 
- What this project does (receives and decodes signals from TFA Dostmann 30.3208.02 and Global Tronics GT-WT-02 weather stations)
- Why it might be useful (integration into ESP8266/ESP32, MQTT, home automation, etc.)

- Big differnz to other librarys, which decode in the ISR, while this code saves the Pulse to an ringbuffer and later decodes them

## Hardware
### MCUs
All codes where written for Esp8266, it should work with Esp32 and Arduino AVR boards.
You may need to do some changes:

AVR:
- changing ISR to standart function
- changing oder delting Serial.printf() functions
- changing to standart digitalRead() function in handleInterrupt() function

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
adds wifi + mqtt functionality
the data is send as an JSON in this topic: TFA433/data

Esp8266_TFA_GT_MQTT_DHT
adds DHT support

Esp8266_TFA_GT_MQTT_DHT_EEPROM
adds EEPROM to store offset variables, which canbe changed with mqtt commands:

publish "{"showOffsets":1}" to TFA433/cmd to receive current offset values on TFA433/msg
publish "{"channel": 1,"type": "temp","offset": 0}" or "{"channel": 1,"type": "hum","offset": 0}" to TFA433/cmd to change the offset variables.
The Esp answers on TFA433/msg to first send the old offsets followed by the new offsets

it is possible to hard-code default values for the offsets, which are applied, if the EEPROM_INIT_MARKER is changed
change these arrays:

float Default_Temp_Adjust[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
float Default_Hum_Adjust[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

### additional information for the channels
my setup uses 8 channels:
6 TFA channals (max: 8)
1 DHT channel  (max: 1)
1 GT channel  (max: 3)

if you have another setup with differnt amounts off sensors, you can change the DHT channel in the readDHT() function and you can change the  CHANNEL_OFFSET define, to change the GT channels

## Pulse Collection

For the Pulse Collection i use an change interrupt, which feeds this ringbuffer:

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

PulseRingBuffer pulseBuffer;

this is somewhat like an software version of the RMT feature on ESP32.
You could probably change to code to use the RMT buffer, but this a bigger change to the code.

If the buffer overflows it sets the bufferOverflow flag and prints a message to the Serial output. durring an overflow event, all addtional data is lost.
The buffer is only emptyed by the copyPulseBuffer() function, which is called every 250ms.

The big advantage by this system is, that the Interrupt Service Routine (ISR) stays short, instead off decoding the signal in the ISR, my code saves the pulses and later decodes them.

## TFA Decoder

The TFA Dostmann 30.3208 uses an Manchester encoded signal.
The protocoll is described here: 
* https://manual.pilight.org/protocols/433.92/weather/tfa2017.html (not the Dostmann TFA 30.3200 protocoll)
* https://github.com/merbanan/rtl_433/blob/master/src/devices/ambient_weather.c

The decoding off the Pulses is designd by myself, but it is heavly inspired by this Project: https://github.com/victornpb/manch_decode/tree/gh-pages
The decogin off the Bytes is inspired by this library: https://github.com/d10i/TFA433
which i firstly improved to be able to run on ESP boards: https://github.com/Spb2005/TFAReceiver/tree/main
and now further improved in this repo

## GT Decoder

The Gloabal Tronics GT-WT-02 uses an simpler On Off Keying encoded signal.
The protocoll is described here: 
* https://manual.pilight.org/protocols/433.92/weather/teknihall.html
* https://github.com/merbanan/rtl_433/blob/master/src/devices/gt_wt_02.c

I wrote the decoding off the Pulses and the Bytes by myself.

## additional Decoders

You can add your own decoding algorithms. 
I recommend to make an function where the decoding happens (Like the checkTFA() and checkGT() functions). and put this function into the checkBuffer() function, which is called every 250 ms and emptys the ringbuffer. 

## Inspirations and References
As already menchiond i used the Projects as Inspirations and References:

Protocolls:
TFA
* https://manual.pilight.org/protocols/433.92/weather/tfa2017.html (not the Dostmann TFA 30.3200 protocoll)
* https://github.com/merbanan/rtl_433/blob/master/src/devices/ambient_weather.c
GT
* https://manual.pilight.org/protocols/433.92/weather/teknihall.html
* https://github.com/merbanan/rtl_433/blob/master/src/devices/gt_wt_02.c
Code
* https://github.com/victornpb/manch_decode/tree/gh-pages
* https://github.com/d10i/TFA433

## Conclusion
- Summary of what works  
- Known limitations  
- Possible future improvements (turning into a proper Arduino library, supporting more sensors, etc.)  
