#define RXPIN 0
#define BUFFER_SIZE 1024

// GT defines
#define MAX_DURATION 11000
#define MIN_DURATION 100
#define PULSE_START_MIN_DURATION 7500
#define MAX_PULSE_DURATION 7000
#define BIT_THRESHOLD 3000
#define TFA30_NUM_PULSES 29
#define TFA30_NUM_BITS 28
#define TFA30_NUM_BYTES 4

#define TFA30ChannelOffset 8

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
volatile bool bufferOverflow = false;

// Timing gt
unsigned long lastCheck = 0;
unsigned long GTTime = 0;
//timing TFA30
unsigned long TFA30Time = 0;

// TFA30 Variables
int TFA30pulseIndex = 0;
bool TFA30receive = false;
bool TFA30PulseTrainReady = false;
bool TFA30BitsReady = false;
bool TFA30validData = false;
int TFA30bufIndex = 0;
int TFA30bufCount = 0;
uint32_t TFA30pulses[TFA30_NUM_PULSES + 2];
byte TFA30bitsPacked[TFA30_NUM_BYTES];

void handleInterrupt() {
  uint32_t now = micros();
  uint32_t duration = now - lastChange;
  lastChange = now;


  bool pinState = digitalReadFast(RXPIN);

  uint16_t next = (pulseBuffer.writeIndex + 1) % BUFFER_SIZE;

  if (pulseBuffer.count < BUFFER_SIZE) {

    pulseBuffer.buffer[pulseBuffer.writeIndex].time = duration;
    pulseBuffer.buffer[pulseBuffer.writeIndex].level = !pinState;
    pulseBuffer.writeIndex = next;
    pulseBuffer.count++;

  } else {
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

  checkTFA30(localBuf, count);
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

void checkTFA30(PulsePair localBuf[], int count) {
  TFA30bufIndex = 0;

  while (checkForPulseTrainTFA30(localBuf, count)) {

    unsigned long time = millis();
    if (time - TFA30Time < 1000) {
      continue;
    }

    TFA30getBinary();
    if (TFA30BitsReady) {
      TFA30Checksum();
      if (TFA30validData) {
        TFA30Time = millis();
        Result res = TFA30getData();
        printData(res);
      }
    }

    TFA30PulseTrainReady = false;
    TFA30BitsReady = false;
    TFA30validData = false;
  }
}

bool checkForPulseTrainTFA30(PulsePair localBuf[], int count) {
  if (TFA30bufIndex == 0) {
    TFA30bufCount = count;
    TFA30pulseIndex = 0;
    TFA30receive = false;
  }

  while (TFA30bufIndex < TFA30bufCount) {
    if (localBuf[TFA30bufIndex].level) {  //skip high pulselength
      TFA30bufIndex++;
      continue;
    }

    uint32_t durationLow = localBuf[TFA30bufIndex].time;
    TFA30bufIndex++;

    if (durationLow > MAX_DURATION || durationLow < MIN_DURATION) {  //pulse is too long or too short -> start over
      TFA30pulseIndex = 0;
      TFA30receive = false;
      continue;
    }

    if (durationLow >= PULSE_START_MIN_DURATION && durationLow <= MAX_DURATION) {  // pulse has header or footer length
      if (TFA30receive) {
        // End of pulsetrain
        TFA30pulses[TFA30pulseIndex] = durationLow;
        TFA30receive = false;

        if (TFA30pulseIndex == TFA30_NUM_PULSES) {
          // Pulsetrain has correct length
          TFA30PulseTrainReady = true;
          return 1;
        } else {
          TFA30pulseIndex = 0;
        }
      } else {
        // Start of pulsetrain
        TFA30pulseIndex = 0;
        TFA30receive = true;
      }
    }

    if (TFA30receive) {  //Adding bits to buffer
      if (TFA30pulseIndex < TFA30_NUM_PULSES + 2) {
        TFA30pulses[TFA30pulseIndex++] = durationLow;
      } else {
        TFA30receive = false;
        TFA30pulseIndex = 0;
      }
    }
  }

  TFA30bufIndex = 0;  // no more pulsetrains found
  return 0;
}

void TFA30getBinary() {
  memset(TFA30bitsPacked, 0, sizeof(TFA30bitsPacked));

  int bitIndex = 0;

  for (int i = 1; i < TFA30_NUM_PULSES; i++) {
    if (TFA30pulses[i] > MAX_PULSE_DURATION || bitIndex >= TFA30_NUM_BITS) {
      Serial.println("Error!");
      TFA30BitsReady = false;
      return;
    }

    if (TFA30pulses[i] >= BIT_THRESHOLD) {
      TFA30setBitPacked(bitIndex, 1);
    }
    // if pulse is unter BIT_THRESHOLD bit stays 0

    bitIndex++;
  }
  TFA30BitsReady = true;
}

void TFA30Checksum() {
  uint8_t* b = TFA30bitsPacked;
  int sum_nibbles = 0;
  //sum of all nibbles, except the first one(checksum)
  for (int i = 1; i < 7; i++) {
    int nibble;
    if (i % 2 == 0) {
      nibble = b[i / 2] >> 4;
    } else {
      nibble = b[i / 2] & 0xF;
    }
    sum_nibbles += nibble;
  }
  //calculate Checksum
  int calculated = (sum_nibbles - 1) & 0xF;

  // expected checksum from first nibble
  int expected = b[0] >> 4;

  if (expected == calculated) {
    TFA30validData = true;
    //Serial.println("Checksum ok!");
  } else {
    TFA30validData = false;
    Serial.printf("TFA30 Checksum not OK! expected: %d calculated: %d\n", expected, calculated);
  }
}


Result TFA30getData() {
  Result data;
  data.type = 3;
  data.id = binToDec(4, 11, TFA30bitsPacked);
  data.battery = getBit(26, TFA30bitsPacked);
  data.channel = binToDec(24, 25, TFA30bitsPacked) + TFA30ChannelOffset;
  data.temperature = binToSigned12(12, TFA30bitsPacked) / 10.0;
  data.humidity = 0;
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
  return (msg[i] & (0b10000000 >> pos)) != 0;
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

void TFA30printBits() {
  for (int i = 0; i < TFA30_NUM_BITS; i++) {
    Serial.print(TFA30getBitPacked(i));
    if (i % 8 == 7) Serial.print(" ");
  }
  Serial.println();
}

inline void TFA30setBitPacked(int index, bool value) {
  int byteIndex = index / 8;
  int bitIndex = 7 - (index % 8);  // MSB first
  if (value) {
    TFA30bitsPacked[byteIndex] |= (1 << bitIndex);
  } else {
    TFA30bitsPacked[byteIndex] &= ~(1 << bitIndex);
  }
}

inline bool TFA30getBitPacked(int index) {
  int byteIndex = index / 8;
  int bitIndex = 7 - (index % 8);
  return (TFA30bitsPacked[byteIndex] >> bitIndex) & 1;
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
