#define RXPIN 13
#define BUFFER_SIZE 1024

//GT defines
#define MAX_DURATION 10000  // µs
#define MIN_DURATION 100    // µs
#define PULSE_START_MIN_DURATION 7500
#define MAX_PULSE_DURATION 7000
#define BIT_THRESHOLD 3000
#define NUM_PULSES 38  // Anzahl erwarteter Datenpulse
#define NUM_BITS 37
#define NUM_BYTES 5  // 8*5 bytes = 40 bits > 37 bits

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
PulsePair localBuf[BUFFER_SIZE];

struct Result {
  byte type;
  byte id;
  bool battery;
  byte channel;
  float temperature;
  byte humidity;
};

volatile uint32_t lastChange = 0;
volatile bool bufferOverflow = 0;

//Timestamps
unsigned long lastCheck = 0;
unsigned long GTTime = 0;

//GT Variables
int GTpulseIndex = 0;
bool GTreceive = false;
bool GTPulseTrainReady = false;
bool GTBitsReady = false;
bool GTvalidData = false;
int GTbufIndex = 0;
int GTbufCount = 0;
uint32_t GTpulses[NUM_PULSES + 2];
byte GTbitsPacked[NUM_BYTES];

ICACHE_RAM_ATTR void handleInterrupt() {
  uint32_t now = micros();
  uint32_t duration = now - lastChange;
  lastChange = now;

  uint32_t gpioState = GPIO_REG_READ(GPIO_IN_ADDRESS);
  bool pinState = gpioState & (1 << RXPIN);

  uint16_t next = (pulseBuffer.writeIndex + 1) % BUFFER_SIZE;
  if (pulseBuffer.count < BUFFER_SIZE) {

    pulseBuffer.buffer[pulseBuffer.writeIndex].time = duration;
    pulseBuffer.buffer[pulseBuffer.writeIndex].level = !pinState;
    pulseBuffer.writeIndex = next;
    pulseBuffer.count++;

  } else {  //buffer full
    bufferOverflow = true;
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(RXPIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(RXPIN), handleInterrupt, CHANGE);
}

void loop() {
  if (bufferOverflow) {
    Serial.println("Buffer Full!");
    bufferOverflow = false;
  }

  unsigned long time = millis();
  if (time - lastCheck > 250) {
    lastCheck = time;
    checkBuffer();
  }
}

void checkBuffer() {
  int count = copyPulseBuffer(localBuf, BUFFER_SIZE);

  checkGT(localBuf, count);
}

int copyPulseBuffer(PulsePair* dest, int maxCount) {
  noInterrupts();
  int count = pulseBuffer.count;
  int readIndex = pulseBuffer.readIndex;
  if (count > maxCount) count = maxCount;
  for (int i = 0; i < count; i++) {
    dest[i] = pulseBuffer.buffer[readIndex];
    readIndex = (readIndex + 1) % BUFFER_SIZE;
  }
  pulseBuffer.readIndex = (pulseBuffer.readIndex + count) % BUFFER_SIZE;
  pulseBuffer.count -= count;
  interrupts();
  return count;
}

void checkGT(PulsePair localBuf[], int count) {
  GTbufIndex = 0;

  while (checkForPulseTrain(localBuf, count)) {

    unsigned long time = millis();
    if (time - GTTime < 1000) {
      continue;
    }

    GTgetBinary();
    if (GTBitsReady) {
      GTChecksum();
      if (GTvalidData) {
        GTTime = millis();
        Result res = GTgetData();
        printData(res);
      }
    }

    GTPulseTrainReady = false;
    GTBitsReady = false;
    GTvalidData = false;
  }
}

bool checkForPulseTrain(PulsePair localBuf[], int count) {
  if (GTbufIndex == 0) {
    GTbufCount = count;
    GTpulseIndex = 0;
    GTreceive = false;
  }

  while (GTbufIndex < GTbufCount) {
    if (localBuf[GTbufIndex].level) {  //skip high pulselength
      GTbufIndex++;
      continue;
    }

    uint32_t durationLow = localBuf[GTbufIndex].time;
    GTbufIndex++;

    if (durationLow > MAX_DURATION || durationLow < MIN_DURATION) {  //pulse is too long or too short -> start over
      GTpulseIndex = 0;
      GTreceive = false;
      continue;
    }

    if (durationLow >= PULSE_START_MIN_DURATION && durationLow <= MAX_DURATION) {  // pulse has header or footer length
      if (GTreceive) {
        // End of pulsetrain
        GTpulses[GTpulseIndex] = durationLow;
        GTreceive = false;

        if (GTpulseIndex == NUM_PULSES) {
          // Pulsetrain has write length
          GTPulseTrainReady = true;
          return 1;
        } else {
          GTpulseIndex = 0;
        }
      } else {
        // Start of pulsetrain
        GTpulseIndex = 0;
        GTreceive = true;
      }
    }

    if (GTreceive) {  //Adding bits to buffer
      if (GTpulseIndex < NUM_PULSES + 2) {
        GTpulses[GTpulseIndex++] = durationLow;
      } else {
        GTreceive = false;
        GTpulseIndex = 0;
      }
    }
  }

  GTbufIndex = 0;  // no more pulsetrains found
  return 0;
}

void GTgetBinary() {
  memset(GTbitsPacked, 0, sizeof(GTbitsPacked));

  int bitIndex = 0;

  for (int i = 1; i < NUM_PULSES; i++) {
    if (GTpulses[i] > MAX_PULSE_DURATION || bitIndex >= NUM_BITS) {
      Serial.println("Error!");
      GTBitsReady = false;
      return;
    }

    if (GTpulses[i] >= BIT_THRESHOLD) {
      setBitPacked(bitIndex, 1);
    }
    // if pulse is unter BIT_THRESHOLD bit stays 0

    bitIndex++;
  }

  GTBitsReady = true;
}

void GTChecksum() {
  uint8_t* b = GTbitsPacked;

  int sum_nibbles = (b[0] >> 4) + (b[0] & 0xF) + (b[1] >> 4) + (b[1] & 0xF) + (b[2] >> 4) + (b[2] & 0xF) + (b[3] >> 4) + (b[3] & 0xE);  // nur obere 3 Bit

  int calculated = sum_nibbles & 0x3F;

  int expected = ((b[3] & 1) << 5) + (b[4] >> 3);  //firt 5 bits of b[4] -> last 5 bits of whole bitstring

  if (expected == calculated) {
    GTvalidData = true;
    //Serial.println("Checksum ok!");
  } else {
    GTvalidData = false;
    Serial.printf("Checksum not OK! expected: %d calculated: %d\n", expected, calculated);
  }
}

Result GTgetData() {
  Result data;
  data.type = 1;
  data.id = binToDec(0, 7, GTbitsPacked);
  data.battery = !getBit(8, GTbitsPacked);
  data.channel = binToDec(10, 11, GTbitsPacked) + 1;
  data.temperature = binToSigned12(12, GTbitsPacked) / 10.0;
  data.humidity = binToDec(24, 30, GTbitsPacked);
  if (data.humidity == 10) {  // display shows LL -> out of measuring range
    data.humidity = 0;
  } else if (data.humidity == 110) {  // display shows HH -> out of measuring range
    data.humidity = 100;
  }
  return data;
}

int binToDec(int s, int e, byte msg[]) {
  int result = 0;
  unsigned int mask = 1;
  for (; e >= s; mask <<= 1, e--) {
    if (getBit(e, msg)) result |= mask;
  }
  return result;
}

bool getBit(int k, byte msg[]) {
  int i = k / 8;
  int pos = k % 8;
  return (msg[i] & (B10000000 >> pos)) != 0;
}

int binToSigned12(int s, byte msg[]) {
  int val = 0;
  for (int i = s; i < s + 12; i++) {
    val = (val << 1) | getBit(i, msg);
  }
  if (val & 0x800) {
    val -= 0x1000;
  }
  return val;
}

void printBits() {
  for (int i = 0; i < NUM_BITS; i++) {
    Serial.print(getBitPacked(i));
    if (i % 8 == 7) Serial.print(" ");
  }
  Serial.println();
}

inline void setBitPacked(int index, bool value) {
  int byteIndex = index / 8;
  int bitIndex = 7 - (index % 8);  // MSB first
  if (value) {
    GTbitsPacked[byteIndex] |= (1 << bitIndex);
  } else {
    GTbitsPacked[byteIndex] &= ~(1 << bitIndex);
  }
}

inline bool getBitPacked(int index) {
  int byteIndex = index / 8;
  int bitIndex = 7 - (index % 8);
  return (GTbitsPacked[byteIndex] >> bitIndex) & 1;
}

void printData(Result res) {
  Serial.print("Type: ");
  Serial.println(res.type);
  Serial.print("ID: ");
  Serial.println(res.id);
  Serial.print("Battery OK: ");
  Serial.println(res.battery);
  Serial.print("Channel: ");
  Serial.println(res.channel);
  Serial.print("Temperature: ");
  Serial.printf("%.1f °C\n", res.temperature);
  Serial.print("Humidity: ");
  Serial.printf("%d %%\n", res.humidity);
  Serial.println("=======================");
}