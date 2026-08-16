// PROMINENT ISOLATED-BENCH-ONLY TARGET.
// This file deliberately contains the only active CAN attempt in the D1 Mini
// project. It must never be linked into the default simulator or vehicle code.
#if !defined(MCAN_BENCH_ONLY) || MCAN_BENCH_ONLY != 1
#error "mcp2515_bench.cpp is restricted to the bench_mcp2515 environment"
#endif

#include <Arduino.h>
#include <SPI.h>

namespace {

// D1 Mini (ESP8266) hardware SPI: SCK=D5/GPIO14, MISO=D6/GPIO12,
// MOSI=D7/GPIO13. D8/GPIO15 is the manually controlled chip select.
constexpr uint8_t kChipSelectPin = D8;
constexpr uint8_t kInterruptPin = D1; // MCP2515 INT, open-drain active low.
constexpr uint32_t kSpiFrequencyHz = 1000000UL;
constexpr uint32_t kOscillatorFrequencyHz = 8000000UL;
constexpr uint32_t kCanBitrateHz = 500000UL;
constexpr uint32_t kAttemptTimeoutMs = 50UL;
constexpr uint16_t kBenchOnlyCanId = 0x123;

// MCP2515 register addresses and SPI commands.
constexpr uint8_t kCanstat = 0x0E;
constexpr uint8_t kCanctrl = 0x0F;
constexpr uint8_t kCnf1 = 0x2A;
constexpr uint8_t kCnf2 = 0x29;
constexpr uint8_t kCnf3 = 0x28;
constexpr uint8_t kCaninte = 0x2B;
constexpr uint8_t kCanintf = 0x2C;
constexpr uint8_t kEflg = 0x2D;
constexpr uint8_t kTxb0ctrl = 0x30;

constexpr uint8_t kConfigMode = 0x80;
constexpr uint8_t kNormalModeOneShot = 0x08;
constexpr uint8_t kTxRequest = 0x08;
constexpr uint8_t kTx0Interrupt = 0x04;

uint8_t read_register(uint8_t address) {
  SPI.beginTransaction(SPISettings(kSpiFrequencyHz, MSBFIRST, SPI_MODE0));
  digitalWrite(kChipSelectPin, LOW);
  SPI.transfer(0x03); // READ
  SPI.transfer(address);
  const uint8_t value = SPI.transfer(0x00);
  digitalWrite(kChipSelectPin, HIGH);
  SPI.endTransaction();
  return value;
}

void write_register(uint8_t address, uint8_t value) {
  SPI.beginTransaction(SPISettings(kSpiFrequencyHz, MSBFIRST, SPI_MODE0));
  digitalWrite(kChipSelectPin, LOW);
  SPI.transfer(0x02); // WRITE
  SPI.transfer(address);
  SPI.transfer(value);
  digitalWrite(kChipSelectPin, HIGH);
  SPI.endTransaction();
}

void reset_controller() {
  SPI.beginTransaction(SPISettings(kSpiFrequencyHz, MSBFIRST, SPI_MODE0));
  digitalWrite(kChipSelectPin, LOW);
  SPI.transfer(0xC0); // RESET
  digitalWrite(kChipSelectPin, HIGH);
  SPI.endTransaction();
  delay(10);
}

void load_one_shot_frame() {
  // LOAD TX BUFFER 0: one standard, zero-data frame with a synthetic ID.
  SPI.beginTransaction(SPISettings(kSpiFrequencyHz, MSBFIRST, SPI_MODE0));
  digitalWrite(kChipSelectPin, LOW);
  SPI.transfer(0x40);
  SPI.transfer(static_cast<uint8_t>(kBenchOnlyCanId >> 3));
  SPI.transfer(static_cast<uint8_t>((kBenchOnlyCanId & 0x07U) << 5));
  SPI.transfer(0x00); // EID8
  SPI.transfer(0x00); // EID0
  SPI.transfer(0x00); // DLC=0, data phase absent
  digitalWrite(kChipSelectPin, HIGH);
  SPI.endTransaction();
}

void request_one_shot_transmission() {
  // RTS TXB0 is intentionally issued exactly once from setup().
  SPI.beginTransaction(SPISettings(kSpiFrequencyHz, MSBFIRST, SPI_MODE0));
  digitalWrite(kChipSelectPin, LOW);
  SPI.transfer(0x81); // RTS TXB0
  digitalWrite(kChipSelectPin, HIGH);
  SPI.endTransaction();
}

bool configure_controller() {
  reset_controller();
  if ((read_register(kCanctrl) & 0xE0U) != kConfigMode ||
      (read_register(kCanstat) & 0xE0U) != kConfigMode) {
    Serial.println(F("MCP2515 reset/config mode check FAILED; no CAN attempt"));
    return false;
  }

  // These timing bytes are the documented 8 MHz / 500 kbit/s configuration.
  // The oscillator and bitrate remain NOT RUN until verified on the bench.
  write_register(kCnf1, 0x00);
  write_register(kCnf2, 0x90);
  write_register(kCnf3, 0x02);
  if (read_register(kCnf1) != 0x00U || read_register(kCnf2) != 0x90U ||
      read_register(kCnf3) != 0x02U) {
    Serial.println(F("MCP2515 SPI register readback FAILED; no CAN attempt"));
    return false;
  }
  write_register(kCaninte, kTx0Interrupt);
  write_register(kCanctrl, kNormalModeOneShot);

  const uint8_t canstat = read_register(kCanstat);
  if ((canstat & 0xE0U) != 0x00U) {
    Serial.println(F("MCP2515 normal-mode check FAILED; no CAN attempt"));
    return false;
  }
  Serial.printf("MCP2515 configured: oscillator=%luHz (PENDING), bitrate=%lu, SPI=%lu\n",
                kOscillatorFrequencyHz, kCanBitrateHz, kSpiFrequencyHz);
  return true;
}

void attempt_once() {
  load_one_shot_frame();
  request_one_shot_transmission();

  const uint32_t started = millis();
  while ((millis() - started) < kAttemptTimeoutMs) {
    if ((read_register(kTxb0ctrl) & kTxRequest) == 0U) {
      break;
    }
    delay(1);
  }

  const uint8_t txb0ctrl = read_register(kTxb0ctrl);
  const uint8_t canintf = read_register(kCanintf);
  const uint8_t eflg = read_register(kEflg);
  Serial.printf("TX_ONESHOT id=0x%03X txb0=0x%02X intf=0x%02X eflg=0x%02X int=%d\n",
                kBenchOnlyCanId, txb0ctrl, canintf, eflg, digitalRead(kInterruptPin));
  Serial.println(F("No retry is permitted; interpret ACK/error only on the isolated bench"));
}

bool attempt_made = false;
int last_interrupt_level = HIGH;

} // namespace

void setup() {
  Serial.begin(115200);
  pinMode(kChipSelectPin, OUTPUT);
  digitalWrite(kChipSelectPin, HIGH);
  pinMode(kInterruptPin, INPUT_PULLUP);
  SPI.begin();
  Serial.println(F("BENCH ONLY: MCP2515/TJA1050; never connect to a vehicle"));
  Serial.println(F("Safety: shared-VCC module must be 3.3V while SPI/INT are connected"));

  if (configure_controller() && !attempt_made) {
    attempt_made = true;
    attempt_once();
  }
  last_interrupt_level = digitalRead(kInterruptPin);
}

void loop() {
  // Observe INT transitions without issuing another frame or retrying.
  const int interrupt_level = digitalRead(kInterruptPin);
  if (interrupt_level != last_interrupt_level) {
    Serial.printf("INT level changed: %s\n", interrupt_level == LOW ? "LOW" : "HIGH");
    last_interrupt_level = interrupt_level;
  }
  delay(10);
}
