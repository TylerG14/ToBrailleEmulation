#include <Arduino.h>

// Arduino pins wired to 74HC595 control signals in SimulIDE (use Arduino header numbers)
int dataPin = 8;
int clockPin = 9;
int latchPin = 10;

// Choose bit order and inversion if needed for your wiring
const uint8_t SHIFT_ORDER = MSBFIRST; // try MSBFIRST if bits appear reversed
const bool INVERT_OUTPUTS = false;    // true if LED ON == output LOW (common-anode)

// Standard mapping: bit0 = dot1, bit1 = dot2, ... bit5 = dot6 (first 74HC595 - nearest to MCU)
const uint8_t DOT1 = 1<<0;
const uint8_t DOT2 = 1<<1;
const uint8_t DOT3 = 1<<2;
const uint8_t DOT4 = 1<<3;
const uint8_t DOT5 = 1<<4;
const uint8_t DOT6 = 1<<5;

// Second (separate) 74HC595 (chained): we will use bits 0..6 there to drive a 7-segment display (a..g).
// If you wired differently, change these segment bit definitions to match Q0..Q7 connections.
const uint8_t SEG_A  = 1<<0; // second chip Q0
const uint8_t SEG_B  = 1<<1; // second chip Q1
const uint8_t SEG_C  = 1<<2; // second chip Q2
const uint8_t SEG_D  = 1<<3; // second chip Q3
const uint8_t SEG_E  = 1<<4; // second chip Q4
const uint8_t SEG_F  = 1<<5; // second chip Q5
const uint8_t SEG_G  = 1<<6; // second chip Q6
const uint8_t SEG_DP = 1<<7; // optional decimal point on Q7

// Hard-coded character to display after diagnostics
const char DISPLAY_CHAR = 'a';
const uint8_t OE_PIN = 4;     // wire 74HC595 /OE to Arduino D4 (or set 255 if tied to GND)
const uint8_t SRCLR_PIN = 5;  // wire 74HC595 /SRCLR (MR) to Arduino D5 (or set 255 if tied to VCC)

// Rotary encoder pins and state (uses D2/D3)
const uint8_t ENC_PIN_A = 2;
const uint8_t ENC_PIN_B = 3;
volatile int encoderPos = 0;
volatile bool encoderMoved = false;
const int ENC_MIN = 0;
const int ENC_MAX = 9;

// Keypad (4x3) wiring (match your SimulIDE layout)
// rows = pins on left of keypad part (drive LOW one row at a time)
const uint8_t KEYPAD_ROW_PINS[4] = {6, 7, 11, 12};  // top -> bottom on part
const uint8_t KEYPAD_COL_PINS[3] = {A0, A1, A2};    // columns (use INPUT_PULLUP)
const char KEYPAD_MAP[12] = { '1','2','3','4','5','6','7','8','9','*','0','#' };
const uint8_t KEYPAD_ROWS = 4;
const uint8_t KEYPAD_COLS = 3;
const uint8_t KEY_INDICATOR_PIN = LED_BUILTIN; // visual output when a key is pressed

// Speaker (piezo) pinned to A3 (change if you prefer different Arduino pin)
const uint8_t SPEAKER_PIN = A3;

// runtime state (so we don't overwrite braille when updating number)
uint8_t currentBraillePattern = 0; // byte for first (nearest) 74HC595
uint8_t currentNumberPattern = 0;  // byte for second (farthest) 74HC595
char lastKey = 0;
bool speakerOn = false;
// New: track whether the current braille pattern is meant to be a numeric digit
bool brailleIsNumeric = false;

void setup() {
  Serial.begin(115200);
  pinMode(dataPin, OUTPUT);
  pinMode(clockPin, OUTPUT);
  pinMode(latchPin, OUTPUT);
  digitalWrite(latchPin, LOW);

  if (OE_PIN != 255) { pinMode(OE_PIN, OUTPUT); digitalWrite(OE_PIN, LOW); }    // enable outputs
  if (SRCLR_PIN != 255) { pinMode(SRCLR_PIN, OUTPUT); digitalWrite(SRCLR_PIN, HIGH); } // not cleared

  // Force-clear shift registers outputs (guarantee known state on both chained chips)
  digitalWrite(latchPin, LOW);
  // send two bytes: first byte goes to farthest chip, second to nearest (we clear both)
  shiftOut(dataPin, clockPin, SHIFT_ORDER, 0x00); // farthest (second) 595
  shiftOut(dataPin, clockPin, SHIFT_ORDER, 0x00); // nearest (first) 595 (braille)
  digitalWrite(latchPin, HIGH);
  delay(50);

  // set up encoder inputs (use internal pullups)
  pinMode(ENC_PIN_A, INPUT_PULLUP);
  pinMode(ENC_PIN_B, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENC_PIN_A), encoderISR, CHANGE);

  // --- keypad init ---
  pinMode(KEY_INDICATOR_PIN, OUTPUT);
  digitalWrite(KEY_INDICATOR_PIN, LOW);
  for (uint8_t c = 0; c < KEYPAD_COLS; ++c) {
    pinMode(KEYPAD_COL_PINS[c], INPUT_PULLUP);
  }
  for (uint8_t r = 0; r < KEYPAD_ROWS; ++r) {
    pinMode(KEYPAD_ROW_PINS[r], OUTPUT);
    digitalWrite(KEYPAD_ROW_PINS[r], HIGH); // inactive
  }

  // speaker pin
  pinMode(SPEAKER_PIN, OUTPUT);
  digitalWrite(SPEAKER_PIN, LOW);

  Serial.println("Starting single-bit diagnostic: pulses 0..5 (dot1..dot6).");
  // performSingleBitTest(); // disabled by default

  // display hard-coded char (store and update safely)
  uint8_t pat = patternForChar(DISPLAY_CHAR);
  if (pat == 0xFF) { Serial.println("Character not found — clearing display."); pat = 0x00; }
  Serial.print("Displaying (hard-coded): "); Serial.println(DISPLAY_CHAR);
  currentBraillePattern = pat;
  // set numeric flag only if DISPLAY_CHAR is a digit
  brailleIsNumeric = (DISPLAY_CHAR >= '0' && DISPLAY_CHAR <= '9');

  // show initial encoder value on number register (does not clobber braille)
  // do not display a number by default (avoids unwanted buzzer); encoder updates will set this
  currentNumberPattern = 0x00;

  updateOutputs();
}

void loop() {
  // update display when encoder changes
  if (encoderMoved) {
    noInterrupts();
    int val = encoderPos;
    encoderMoved = false;
    interrupts();
    currentNumberPattern = patternForNumberDisplay(val);
    updateOutputs();
    Serial.print("Encoder -> ");
    Serial.println(val);
  }

  // --- keypad handling: visual + serial + show digit on 7-seg if numeric ---
  char k = keypadScan(); // returns 0 if none
  if (k != 0) {
    digitalWrite(KEY_INDICATOR_PIN, HIGH);
    if (k != lastKey) {
      lastKey = k;
      Serial.print("Key pressed: ");
      Serial.println(k);
      if (k >= '0' && k <= '9') {
        currentNumberPattern = patternForNumberDisplay(k - '0');
        updateOutputs();
      }
    }
  } else {
    digitalWrite(KEY_INDICATOR_PIN, LOW);
    if (lastKey != 0) { lastKey = 0; } // reset
  }

  // Speaker behavior: buzz only when the 6-LED braille pattern represents a number
  bool shouldBuzz = brailleRepresentsDigit();
  if (shouldBuzz && !speakerOn) {
    // start continuous tone (frequency = 1000 Hz); change freq if desired
    tone(SPEAKER_PIN, 1000);
    speakerOn = true;
  } else if (!shouldBuzz && speakerOn) {
    noTone(SPEAKER_PIN);
    speakerOn = false;
  }

  // small idle sleep
  delay(10);
}

void performSingleBitTest() {
  // test braille bits only (first/nearest 74HC595), keep number register off
  for (uint8_t bit = 0; bit < 6; ++bit) {
    uint8_t pattern = (1 << bit);
    Serial.print("Test bit ");
    Serial.print(bit);
    Serial.print(" -> pattern 0b");
    Serial.println(pattern, BIN);
    currentBraillePattern = pattern;
    // tests are not numeric
    brailleIsNumeric = false;
    currentNumberPattern = 0x00;
    updateOutputs();
    delay(800);
  }
  // clear after test
  currentBraillePattern = 0x00;
  brailleIsNumeric = false;
  currentNumberPattern = 0x00;
  updateOutputs();
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
    case '1': return DOT1;                     // 1
    case '2': return DOT1|DOT2;                // 1,2
    case '3': return DOT1|DOT4;                // 1,4
    case '4': return DOT1|DOT4|DOT5;           // 1,4,5
    case '5': return DOT1|DOT5;                // 1,5
    case '6': return DOT1|DOT2|DOT4;           // 1,2,4
    case '7': return DOT1|DOT2|DOT4|DOT5;      // 1,2,4,5
    case '8': return DOT1|DOT2|DOT5;           // 1,2,5
    case '9': return DOT2|DOT4;                // 2,4
    case '0': return DOT2|DOT4|DOT5;           // 2,4,5
    default:  return 0xFF;
  }
}

// update both chained 74HC595 outputs at once:
// - first byte shifted becomes the farthest (second) 74HC595
// - second byte shifted becomes the nearest (first) 74HC595 (braille)
void updateOutputs() {
  uint8_t numberByte = currentNumberPattern;
  uint8_t brailleByte = currentBraillePattern;

  uint8_t outNumber = numberByte;
  uint8_t outBraille = brailleByte;
  if (INVERT_OUTPUTS) {
    outNumber = ~outNumber;
    outBraille = ~outBraille;
  }

  digitalWrite(latchPin, LOW);
  // shift order: send farthest first (number register), then nearest (braille)
  shiftOut(dataPin, clockPin, SHIFT_ORDER, outNumber);
  shiftOut(dataPin, clockPin, SHIFT_ORDER, outBraille);
  digitalWrite(latchPin, HIGH);

  Serial.print("Number reg: 0b");
  Serial.print(numberByte, BIN);
  Serial.print("  Braille reg: 0b");
  Serial.print(brailleByte, BIN);
  Serial.print("  (sent number then braille)");
  Serial.println();
}

// simple encoder ISR: when A changes, read B to determine direction
void encoderISR() {
  uint8_t a = digitalRead(ENC_PIN_A);
  uint8_t b = digitalRead(ENC_PIN_B);
  if (a == b) { // one direction (clockwise)
    if (encoderPos < ENC_MAX) ++encoderPos;
  } else {      // other direction (counter-clockwise)
    if (encoderPos > ENC_MIN) --encoderPos;
  }
  encoderMoved = true;
}

// scan 4x3 keypad matrix and return the mapped char, or 0 if none
char keypadScan() {
  for (uint8_t r = 0; r < KEYPAD_ROWS; ++r) {
    digitalWrite(KEYPAD_ROW_PINS[r], LOW);      // activate row
    delayMicroseconds(5);                       // allow settle
    for (uint8_t c = 0; c < KEYPAD_COLS; ++c) {
      if (digitalRead(KEYPAD_COL_PINS[c]) == LOW) {
        delay(12); // debounce
        bool still = (digitalRead(KEYPAD_COL_PINS[c]) == LOW);
        digitalWrite(KEYPAD_ROW_PINS[r], HIGH); // deactivate before returning
        if (still) {
          uint8_t idx = r * KEYPAD_COLS + c;
          if (idx < sizeof(KEYPAD_MAP)) return KEYPAD_MAP[idx];
          return 0;
        }
      }
    }
    digitalWrite(KEYPAD_ROW_PINS[r], HIGH);     // deactivate row
  }
  return 0;
}

// map a number (0-9) to a pattern placed into the second (farthest) 74HC595.
// Standard 7-segment (segments a..g). '1' lights segments B and C, etc.
// Adjust values if your segment wiring order is different.
uint8_t patternForNumberDisplay(int n) {
  if (n < 0) n = 0;
  if (n > 9) n = 9;
  static const uint8_t segDigits[10] = {
    // gfedcba  (bit order shown: SEG_G..SEG_A)
    // We build in SEG_* order (a = LSB)
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,          // 0
    SEG_B | SEG_C,                                          // 1
    SEG_A | SEG_B | SEG_D | SEG_E | SEG_G,                  // 2
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_G,                  // 3
    SEG_F | SEG_G | SEG_B | SEG_C,                          // 4
    SEG_A | SEG_F | SEG_G | SEG_C | SEG_D,                  // 5
    SEG_A | SEG_F | SEG_E | SEG_D | SEG_C | SEG_G,          // 6
    SEG_A | SEG_B | SEG_C,                                  // 7
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G,  // 8
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_F | SEG_G           // 9
  };
  return segDigits[n];
}

// return true if currentBraillePattern equals any braille digit pattern (1..9,0)
// and the display is currently in numeric mode.
bool brailleRepresentsDigit() {
  if (!brailleIsNumeric) return false;
  for (char d = '1'; d <= '9'; ++d) {
    if (currentBraillePattern == patternForChar(d)) return true;
  }
  if (currentBraillePattern == patternForChar('0')) return true;
  return false;
}