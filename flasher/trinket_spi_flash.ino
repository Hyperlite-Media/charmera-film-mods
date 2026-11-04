// Trinket M0 SPI NOR flash reader/writer for the Puya PY25D16HB (2 MB).
//
// Wiring to the SOIC-8 flash (pin 1 = dot):
//   Flash 1 CS#   -> Trinket D1
//   Flash 2 DO    -> Trinket D2 (MISO)
//   Flash 3 WP#   -> 3V  (only if the chip is off the board)
//   Flash 4 GND   -> Trinket GND
//   Flash 5 DI    -> Trinket D4 (MOSI)
//   Flash 6 CLK   -> Trinket D3 (SCK)
//   Flash 7 HOLD# -> 3V  (only if the chip is off the board)
//   Flash 8 VCC   -> Trinket 3V
//
// Commands over USB serial (use charmera_flash.py on the Mac):
//   'i'  -> prints "ID xx xx xx" (JEDEC manufacturer, type, capacity)
//   's'  -> prints "SR xx" (status register 1)
//   'd'  -> sends 4-byte little-endian length, then the raw flash contents
//   'w'  -> receives a 4-byte little-endian address (4 KB aligned) and 4096 bytes,
//           erases that one sector, programs it, then replies 'K', the 4096 bytes
//           read back and a final '.', or 'E' followed by an error line.
//
// Only 'w' changes the flash, and only the single 4 KB sector it is given.

#include <SPI.h>

const int FLASH_CS = 1;
const uint32_t DEFAULT_SIZE = 2UL * 1024 * 1024;
const uint32_t SECTOR = 4096;
const uint32_t PAGE = 256;
SPISettings flashSpi(4000000, MSBFIRST, SPI_MODE0);

uint8_t sectorBuf[SECTOR];

void readJedecId(uint8_t id[3]) {
  SPI.beginTransaction(flashSpi);
  digitalWrite(FLASH_CS, LOW);
  SPI.transfer(0x9F);
  for (int i = 0; i < 3; i++) id[i] = SPI.transfer(0x00);
  digitalWrite(FLASH_CS, HIGH);
  SPI.endTransaction();
}

uint32_t flashSize() {
  uint8_t id[3];
  readJedecId(id);
  // Capacity byte is log2(size) on nearly every SPI NOR (0x15 = 2 MB).
  if (id[2] >= 0x10 && id[2] <= 0x18) return 1UL << id[2];
  return DEFAULT_SIZE;
}

uint8_t readStatus() {
  SPI.beginTransaction(flashSpi);
  digitalWrite(FLASH_CS, LOW);
  SPI.transfer(0x05);  // RDSR
  uint8_t sr = SPI.transfer(0x00);
  digitalWrite(FLASH_CS, HIGH);
  SPI.endTransaction();
  return sr;
}

void sendCommand(uint8_t cmd) {
  SPI.beginTransaction(flashSpi);
  digitalWrite(FLASH_CS, LOW);
  SPI.transfer(cmd);
  digitalWrite(FLASH_CS, HIGH);
  SPI.endTransaction();
}

void sendAddress(uint32_t addr) {
  SPI.transfer((uint8_t)(addr >> 16));
  SPI.transfer((uint8_t)(addr >> 8));
  SPI.transfer((uint8_t)addr);
}

// Sets the write-enable latch and checks it took; fails if the chip is write protected.
bool writeEnable() {
  sendCommand(0x06);  // WREN
  return readStatus() & 0x02;
}

bool waitReady(uint32_t timeoutMs) {
  uint32_t start = millis();
  while (readStatus() & 0x01) {
    if (millis() - start > timeoutMs) return false;
  }
  return true;
}

void readRange(uint32_t addr, uint8_t *out, uint32_t len) {
  SPI.beginTransaction(flashSpi);
  digitalWrite(FLASH_CS, LOW);
  SPI.transfer(0x03);  // READ
  sendAddress(addr);
  for (uint32_t i = 0; i < len; i++) out[i] = SPI.transfer(0x00);
  digitalWrite(FLASH_CS, HIGH);
  SPI.endTransaction();
}

void dumpFlash() {
  uint32_t size = flashSize();
  uint8_t header[4] = {
    (uint8_t)size, (uint8_t)(size >> 8), (uint8_t)(size >> 16), (uint8_t)(size >> 24)
  };
  Serial.write(header, 4);

  uint8_t buf[256];
  SPI.beginTransaction(flashSpi);
  digitalWrite(FLASH_CS, LOW);
  SPI.transfer(0x03);  // READ, 24-bit address 0; NOR streams the whole array sequentially
  sendAddress(0);
  for (uint32_t done = 0; done < size; done += sizeof(buf)) {
    for (size_t i = 0; i < sizeof(buf); i++) buf[i] = SPI.transfer(0x00);
    Serial.write(buf, sizeof(buf));
    digitalWrite(LED_BUILTIN, (done >> 15) & 1);
  }
  digitalWrite(FLASH_CS, HIGH);
  SPI.endTransaction();
  digitalWrite(LED_BUILTIN, LOW);
}

bool receiveExact(uint8_t *out, uint32_t len, uint32_t timeoutMs) {
  uint32_t got = 0, last = millis();
  while (got < len) {
    int n = Serial.available();
    if (n > 0) {
      got += Serial.readBytes((char *)out + got, min((uint32_t)n, len - got));
      last = millis();
    } else if (millis() - last > timeoutMs) {
      return false;
    }
  }
  return true;
}

void fail(const char *msg) {
  Serial.write('E');
  Serial.print(msg);
  Serial.print('\n');
}

void writeSector() {
  uint8_t a[4];
  if (!receiveExact(a, 4, 2000)) return fail("timeout receiving address");
  uint32_t addr = a[0] | (a[1] << 8) | ((uint32_t)a[2] << 16) | ((uint32_t)a[3] << 24);
  if (!receiveExact(sectorBuf, SECTOR, 3000)) return fail("timeout receiving data");
  if (addr % SECTOR || addr >= flashSize()) return fail("bad sector address");

  digitalWrite(LED_BUILTIN, HIGH);
  if (!writeEnable()) return fail("write enable failed (chip write protected?)");
  SPI.beginTransaction(flashSpi);
  digitalWrite(FLASH_CS, LOW);
  SPI.transfer(0x20);  // SECTOR ERASE 4 KB
  sendAddress(addr);
  digitalWrite(FLASH_CS, HIGH);
  SPI.endTransaction();
  if (!waitReady(2000)) return fail("erase timed out");

  for (uint32_t off = 0; off < SECTOR; off += PAGE) {
    bool blank = true;
    for (uint32_t i = 0; i < PAGE; i++) blank &= sectorBuf[off + i] == 0xFF;
    if (blank) continue;  // erased pages are already all FF
    if (!writeEnable()) return fail("write enable failed during program");
    SPI.beginTransaction(flashSpi);
    digitalWrite(FLASH_CS, LOW);
    SPI.transfer(0x02);  // PAGE PROGRAM
    sendAddress(addr + off);
    for (uint32_t i = 0; i < PAGE; i++) SPI.transfer(sectorBuf[off + i]);
    digitalWrite(FLASH_CS, HIGH);
    SPI.endTransaction();
    if (!waitReady(100)) return fail("page program timed out");
  }
  digitalWrite(LED_BUILTIN, LOW);

  readRange(addr, sectorBuf, SECTOR);
  Serial.write('K');
  for (uint32_t off = 0; off < SECTOR; off += PAGE) Serial.write(sectorBuf + off, PAGE);
  Serial.write('.');  // short final packet; without it macOS holds the 4096 bytes back
}

void setup() {
  pinMode(FLASH_CS, OUTPUT);
  digitalWrite(FLASH_CS, HIGH);
  pinMode(LED_BUILTIN, OUTPUT);
  SPI.begin();
  Serial.begin(115200);
}

void loop() {
  if (!Serial.available()) return;
  char c = Serial.read();
  char line[32];
  if (c == 'i') {
    uint8_t id[3];
    readJedecId(id);
    snprintf(line, sizeof(line), "ID %02X %02X %02X\n", id[0], id[1], id[2]);
    Serial.print(line);
  } else if (c == 's') {
    snprintf(line, sizeof(line), "SR %02X\n", readStatus());
    Serial.print(line);
  } else if (c == 'd') {
    dumpFlash();
  } else if (c == 'w') {
    writeSector();
  }
}
