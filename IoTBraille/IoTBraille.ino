#include <Arduino.h>

// Arduino pins wired to 74HC595 control signals in SimulIDE (use Arduino header numbers)
int dataPin = 8;
int clockPin = 9;
int latchPin = 10;

// Choose bit order and inversion if needed for your wiring
const uint8_t SHIFT_ORDER = MSBFIRST; // try MSBFIRST if bits appear reversed
const bool INVERT_OUTPUTS = false;    // true if LED ON == output LOW (common-anode)

// Standard mapping: bit0 = dot1, bit1 = dot2, ... bit5 = dot6
const uint8_t DOT1 = 1<<0;
const uint8_t DOT2 = 1<<1;
const uint8_t DOT3 = 1<<2;
const uint8_t DOT4 = 1<<3;
const uint8_t DOT5 = 1<<4;
const uint8_t DOT6 = 1<<5;

// Hard-coded character to display after diagnostics
const char DISPLAY_CHAR = 'z';
const uint8_t OE_PIN = 4;     // wire 74HC595 /OE to Arduino D4 (or set 255 if tied to GND)
const uint8_t SRCLR_PIN = 5;  // wire 74HC595 /SRCLR (MR) to Arduino D5 (or set 255 if tied to VCC)

void setup() {
  Serial.begin(115200);
  pinMode(dataPin, OUTPUT);
  pinMode(clockPin, OUTPUT);
  pinMode(latchPin, OUTPUT);
  digitalWrite(latchPin, LOW);

  if (OE_PIN != 255) { pinMode(OE_PIN, OUTPUT); digitalWrite(OE_PIN, LOW); }    // enable outputs
  if (SRCLR_PIN != 255) { pinMode(SRCLR_PIN, OUTPUT); digitalWrite(SRCLR_PIN, HIGH); } // not cleared

  // Force-clear shift register outputs (guarantee known state)
  digitalWrite(latchPin, LOW);
  shiftOut(dataPin, clockPin, SHIFT_ORDER, 0x00); // all zeros -> all ULN inputs LOW -> LEDs off (if ULN sinks on HIGH)
  digitalWrite(latchPin, HIGH);
  delay(50);

  Serial.println("Starting single-bit diagnostic: pulses 0..5 (dot1..dot6).");
  //  performSingleBitTest(); // disabled: comment out to stop the sequential pulses

  // then display hard-coded char
  uint8_t pat = patternForChar(DISPLAY_CHAR);
  if (pat == 0xFF) { Serial.println("Character not found — clearing display."); pat = 0x00; }
  Serial.print("Displaying (hard-coded): "); Serial.println(DISPLAY_CHAR);
  shiftOutCharPattern(pat);
}

void loop() {
  // static display
  delay(1000);
}

void performSingleBitTest() {
  for (uint8_t bit = 0; bit < 6; ++bit) {
    uint8_t pattern = (1 << bit);
    Serial.print("Test bit ");
    Serial.print(bit);
    Serial.print(" -> pattern 0b");
    Serial.println(pattern, BIN);
    shiftOutCharPattern(pattern);
    delay(800);
  }
  // clear after test
  shiftOutCharPattern(0x00);
  delay(300);
}

uint8_t patternForChar(char c) {
  switch (tolower((unsigned char)c)) {
    case 'a': return DOT1;                     // 1
    case 'b': return DOT1|DOT2;                // 1,2
    case 'c': return DOT1|DOT4;                // 1,4
    case 'd': return DOT1|DOT4|DOT5;           // 1,4,5
    case 'e': return DOT1|DOT5;                // 1,5
    case 'f': return DOT1|DOT2|DOT4;           // 1,2,4
    case 'g': return DOT1|DOT2|DOT4|DOT5;      // 1,2,4,5
    case 'h': return DOT1|DOT2|DOT5;           // 1,2,5
    case 'i': return DOT2|DOT4;                // 2,4
    case 'j': return DOT2|DOT4|DOT5;           // 2,4,5
    case 'k': return DOT1|DOT3;                // 1,3
    case 'l': return DOT1|DOT2|DOT3;           // 1,2,3
    case 'm': return DOT1|DOT3|DOT4;           // 1,3,4
    case 'n': return DOT1|DOT3|DOT4|DOT5;      // 1,3,4,5
    case 'o': return DOT1|DOT3|DOT5;           // 1,3,5
    case 'p': return DOT1|DOT2|DOT3|DOT4;      // 1,2,3,4
    case 'q': return DOT1|DOT2|DOT3|DOT4|DOT5; // 1,2,3,4,5
    case 'r': return DOT1|DOT2|DOT3|DOT5;      // 1,2,3,5
    case 's': return DOT2|DOT3|DOT4;           // 2,3,4
    case 't': return DOT2|DOT3|DOT4|DOT5;      // 2,3,4,5
    case 'u': return DOT1|DOT3|DOT6;           // 1,3,6
    case 'v': return DOT1|DOT2|DOT3|DOT6;      // 1,2,3,6
    case 'w': return DOT2|DOT4|DOT5|DOT6;      // 2,4,5,6
    case 'x': return DOT1|DOT3|DOT4|DOT6;      // 1,3,4,6
    case 'y': return DOT1|DOT3|DOT4|DOT5|DOT6; // 1,3,4,5,6
    case 'z': return DOT1|DOT3|DOT5|DOT6;      // 1,3,5,6
    default:  return 0xFF;
  }
}

void shiftOutCharPattern(uint8_t value) {
  if (INVERT_OUTPUTS) value = ~value;
  digitalWrite(latchPin, LOW);
  shiftOut(dataPin, clockPin, SHIFT_ORDER, value);
  digitalWrite(latchPin, HIGH);
  Serial.print("Shifted out: 0b");
  Serial.println(value, BIN);
}