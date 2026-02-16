#define RXPIN 13
#define BUFFER_SIZE 1024
#define CHANNEL_OFFSET 6
#define USE_CELSIUS 1

//Manchester decoding for TFA
#define TFA_TYPE 69
#define MANCHESTER_CLOCK 500
#define WAIT_PREAMBLE 1
#define SKIP_LONG 2
#define DECODE_DATA 3
#define DONE 4
#define MANCHESTER_BUF_SIZE 30  //5* 6 Bytes

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
unsigned long lastTFATime = 0;
unsigned long GTTime = 0;

//TFA Variables
bool TFAisRepeat = true;
bool TFAChecksumOK = false;
byte lastTFABytes[6] = { 0, 0, 0, 0, 0, 0 };

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


void handleInterrupt() {
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

  } else {  // buffer full
    bufferOverflow = true;
  }
}

// Manchester decoding buffer
namespace Buffer {
volatile uint8_t queue[MANCHESTER_BUF_SIZE];
volatile uint8_t head = 0;
volatile uint8_t tail = 0;

void enqueue(uint8_t b) {
  uint8_t next = (head + 1) % MANCHESTER_BUF_SIZE;
  if (next != tail) {
    queue[head] = b;
    head = next;
  }
}
uint8_t dequeue() {
  if (head == tail) return 0;
  uint8_t b = queue[tail];
  tail = (tail + 1) % MANCHESTER_BUF_SIZE;
  return b;
}
uint8_t queuelevel() {
  if (head >= tail) return head - tail;
  return MANCHESTER_BUF_SIZE - tail + head;
}
}

//Manchester decoding
namespace Manch {

static int status = WAIT_PREAMBLE;

static float T = MANCHESTER_CLOCK;
static uint8_t skipCount = 0;
static uint8_t shortCount = 0;
static uint8_t bitCount = 0;
static bool w = 0;

static byte bits[6];  // 48 Bits = 6 Bytes

inline void resetDecoder() {
  status = WAIT_PREAMBLE;
  bitCount = 0;
  skipCount = 0;
  w = false;
  shortCount = 0;
  for (int i = 0; i < 6; i++) bits[i] = 0;
}

bool isShort(uint16_t t) {
  return t > 0.75 * T && t < 1.25 * T;
}

bool isLong(uint16_t t) {
  return t > 1.75 * T && t < 2.25 * T;
}

void decode(bool s, uint16_t t) {

  if (t < 0.75 * T || t > 2.5 * T) {  //invalid pulse length
    resetDecoder();
    return;
  }

  switch (status) {

    case WAIT_PREAMBLE:
      if (isShort(t)) {  //Count short Pulses off preamble
        shortCount++;
      } else if (isLong(t) && shortCount > 18) {  // if 18 short pulses in sucsession found, look for first long pulse and skip the next long pulse
        status = SKIP_LONG;
        shortCount = 0;
      } else {  //long pulse too early
        shortCount = 0;
      }
      break;

    case SKIP_LONG:
      if (isLong(t)) {  // long puls
        skipCount++;
        if (skipCount >= 1) {  //skip one long pulse
          status = DECODE_DATA;
          bitCount = 0;
          skipCount = 0;
          for (int i = 0; i < 6; i++) bits[i] = 0;
        }
      } else {  //wrong pulse start over
        resetDecoder();
      }
      break;

    case DECODE_DATA:
      // Manchester decoding
      if (t > T * 1.5) {  // 2T → one bit
        bits[bitCount / 8] |= s << (7 - (bitCount % 8));
        bitCount++;
      } else {  // T → two half bits
        if (!w) {
          w = 1;
        } else {
          bits[bitCount / 8] |= s << (7 - (bitCount % 8));
          bitCount++;
          w = 0;
        }
      }

      if (bitCount == 9 && bits[0] != TFA_TYPE) {  //wrong type quick check, in front off the checksum
        Serial.println("Wrong Type");
        resetDecoder();
      }

      if (bitCount >= 48) {
        for (int i = 0; i < 6; i++) Buffer::enqueue(bits[i]);
        status = DONE;
      }
      break;

    case DONE:
      resetDecoder();
      break;
  }
}

uint8_t available() {
  return Buffer::queuelevel();
}

byte read() {
  return Buffer::dequeue();
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

  checkTFA(localBuf, count);

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

//TFA functions
void checkTFA(PulsePair localBuf[], int count) {
  Manch::resetDecoder();  //prevent the decoder to freeze

  for (int i = 0; i < count; i++) {
    Manch::decode(localBuf[i].level, localBuf[i].time);
  }

  while (Manch::available() >= 6) {
    byte msg[6];
    for (int i = 0; i < 6; i++) {
      msg[i] = Manch::read();
    }

    TFACheckRepeat(msg);

    if (!TFAisRepeat) {
      checkTFAchecksum(msg);
      TFAisRepeat = true;
    }

    //printBits(msg);

    if (TFAChecksumOK) {
      Result res = TFAparseResult(msg);
      printData(res);
      TFAChecksumOK = false;
    }
  }
}

void TFACheckRepeat(byte msg[]) {
  unsigned long time = millis();

  if (time - lastTFATime < 1000 && memcmp(msg, lastTFABytes, 6) == 0) {
    lastTFATime = time;
    TFAisRepeat = 1;
    return;
  }

  TFAisRepeat = 0;
  lastTFATime = time;
}

void checkTFAchecksum(byte msg[]) {
  byte expected = msg[5];
  byte calculated = lfsr_digest8(msg, 5, 0x98, 0x3e) ^ 0x64;

  if (expected == calculated) {
    memcpy(lastTFABytes, msg, 6);
    TFAChecksumOK = 1;

  } else {
    Serial.printf("Checksum not OK! expected: %d calculated: %d\n", expected, calculated);
    TFAChecksumOK = 0;
  }
}

byte lfsr_digest8(const byte message[], unsigned n, byte gen, byte key) {  //calculate checksum
  byte sum = 0;
  for (unsigned k = 0; k < n; ++k) {
    byte data = message[k];
    for (int i = 7; i >= 0; --i) {
      if ((data >> i) & 1) sum ^= key;
      key = (key >> 1) ^ (key & 1 ? gen : 0);
    }
  }
  return sum;
}

Result TFAparseResult(byte msg[]) {
  Result r;
  r.type = binToDec(0, 7, msg);
  r.id = binToDec(8, 15, msg);
  r.battery = binToDec(16, 16, msg) != 1;
  r.channel = binToDec(17, 19, msg) + 1;
  float tempf = binToDec(20, 31, msg) / 10.0 - 40.0;
  r.temperature = (USE_CELSIUS) ? ((tempf - 32) * 5.0 / 9.0) : tempf;
  r.humidity = binToDec(32, 39, msg);
  return r;
}

//GT functions
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
  data.channel = binToDec(10, 11, GTbitsPacked) + 1 + CHANNEL_OFFSET;
  float tempC = binToSigned12(12, GTbitsPacked) / 10.0;
  data.temperature = (USE_CELSIUS) ? tempC : (9.0 / 5.0 * tempC) + 32;
  data.humidity = binToDec(24, 30, GTbitsPacked);
  if (data.humidity == 10) {  // display shows LL -> out of measuring range
    data.humidity = 0;
  } else if (data.humidity == 110) {  // display shows HH -> out of measuring range
    data.humidity = 100;
  }
  return data;
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

//default functions
int binToDec(int s, int e, byte msg[]) {
  int result = 0;
  unsigned int mask = 1;
  for (; e > 0 && s <= e; mask <<= 1) {
    if (getBit(e--, msg)) result |= mask;
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

void printBuffer() {
  Serial.println("=====================");
  int count = copyPulseBuffer(localBuf, BUFFER_SIZE);
  Serial.printf("Count: %d\n", count);
  for (int i = 0; i < count; i++) {
    Serial.printf("Duration: %d \t Level: %d\n", localBuf[i].time, localBuf[i].level);
  }
}

void printBits(byte msg[]) {
  Serial.print("Bits: ");
  for (int b = 0; b < 6; b++) {
    for (int bit = 7; bit >= 0; bit--) {
      Serial.print((msg[b] >> bit) & 1);
    }
    if (b < 5) Serial.print(" ");
  }
  Serial.println();
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
  if (USE_CELSIUS) {
    Serial.printf("%.1f °C\n", res.temperature);
  } else {
    Serial.printf("%.1f °F\n", res.temperature);
  }
  Serial.print("Humidity: ");
  Serial.printf("%d %%\n", res.humidity);
  Serial.println("=======================");
}
