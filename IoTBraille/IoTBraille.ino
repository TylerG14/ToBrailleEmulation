#include <Arduino.h>

/* -------------------------
   Pins & Hardware Constants
   ------------------------- */
int dataPin = 8;
int clockPin = 9;
int latchPin = 10;

const uint8_t SHIFT_ORDER = MSBFIRST;
const bool INVERT_OUTPUTS = false;

const uint8_t DOT1 = 1 << 0;
const uint8_t DOT2 = 1 << 1;
const uint8_t DOT3 = 1 << 2;
const uint8_t DOT4 = 1 << 3;
const uint8_t DOT5 = 1 << 4;
const uint8_t DOT6 = 1 << 5;

const uint8_t SEG_A  = 1 << 0;
const uint8_t SEG_B  = 1 << 1;
const uint8_t SEG_C  = 1 << 2;
const uint8_t SEG_D  = 1 << 3;
const uint8_t SEG_E  = 1 << 4;
const uint8_t SEG_F  = 1 << 5;
const uint8_t SEG_G  = 1 << 6;
const uint8_t SEG_DP = 1 << 7;


//Display at launch
const char DISPLAY_CHAR = '1';
const uint8_t OE_PIN = 4;
const uint8_t SRCLR_PIN = 5;

/* -------------------------
   Rotary encoder
   ------------------------- */
const uint8_t ENC_PIN_A = 2;
const uint8_t ENC_PIN_B = 3;
volatile int encoderPos = 0;
volatile bool encoderMoved = false;
const int ENC_MIN = 0;
const int ENC_MAX = 9;

/* -------------------------
   Keypad (4x3) mapping
   ------------------------- */
const uint8_t KEYPAD_ROW_PINS[4] = {6, 7, 11, 12};
const uint8_t KEYPAD_COL_PINS[3] = {A0, A1, A2};
const char KEYPAD_MAP[12] = { '1','2','3','4','5','6','7','8','9','*','0','#' };
const uint8_t KEYPAD_ROWS = 4;
const uint8_t KEYPAD_COLS = 3;
const uint8_t KEY_INDICATOR_PIN = LED_BUILTIN;

/* -------------------------
   Runtime state
   ------------------------- */
uint8_t currentBraillePattern = 0;
uint8_t currentNumberPattern = 0;
char lastKey = 0;
bool keypadMode = true;        // when true, keypad drives braille
bool brailleIsNumeric = false;   // true only when braille was intentionally set to a digit

// Speaker (piezo) on A3; used to distinguish numeric-mode braille from letters
const uint8_t SPEAKER_PIN = A3;
bool speakerOn = false; // start false so tone() can be started when needed

/* -------------------------
   Initialization
   ------------------------- */
void setup() {
  pinMode(dataPin, OUTPUT);
  pinMode(clockPin, OUTPUT);
  pinMode(latchPin, OUTPUT);
  digitalWrite(latchPin, LOW);

  if (OE_PIN != 255) { pinMode(OE_PIN, OUTPUT); digitalWrite(OE_PIN, LOW); }
  if (SRCLR_PIN != 255) { pinMode(SRCLR_PIN, OUTPUT); digitalWrite(SRCLR_PIN, HIGH); }

  // Clear chained shift registers (farthest then nearest)
  digitalWrite(latchPin, LOW);
  shiftOut(dataPin, clockPin, SHIFT_ORDER, 0x00); // farthest (number)
  shiftOut(dataPin, clockPin, SHIFT_ORDER, 0x00); // nearest (braille)
  digitalWrite(latchPin, HIGH);
  delay(50);

  pinMode(ENC_PIN_A, INPUT_PULLUP);
  pinMode(ENC_PIN_B, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENC_PIN_A), encoderISR, CHANGE);

  // Keypad init
  pinMode(KEY_INDICATOR_PIN, OUTPUT);
  digitalWrite(KEY_INDICATOR_PIN, LOW);
  for (uint8_t c = 0; c < KEYPAD_COLS; ++c) {
    pinMode(KEYPAD_COL_PINS[c], INPUT_PULLUP);
  }
  for (uint8_t r = 0; r < KEYPAD_ROWS; ++r) {
    pinMode(KEYPAD_ROW_PINS[r], OUTPUT);
    digitalWrite(KEYPAD_ROW_PINS[r], HIGH);
  }

  // Speaker init
  pinMode(SPEAKER_PIN, OUTPUT);
  digitalWrite(SPEAKER_PIN, LOW);

  // Initial display state depending on mode
  if (keypadMode) {
    currentBraillePattern = 0x00;
    brailleIsNumeric = false;
  } else {
    uint8_t pat = patternForChar(DISPLAY_CHAR);
    if (pat == 0xFF) pat = 0x00;
    currentBraillePattern = pat;
    brailleIsNumeric = (DISPLAY_CHAR >= '0' && DISPLAY_CHAR <= '9');
  }

  currentNumberPattern = 0x00;
  updateOutputs();
}

/* -------------------------
   Main loop
   ------------------------- */
void loop() {
  if (encoderMoved) {
    noInterrupts();
    int val = encoderPos;
    encoderMoved = false;
    interrupts();
    currentNumberPattern = patternForNumberDisplay(val);
    updateOutputs();
  }

  // Keypad handling
  char k = keypadScan();
  if (k != 0) {
    digitalWrite(KEY_INDICATOR_PIN, HIGH);
    if (k != lastKey) {
      lastKey = k;
      if (keypadMode) {
        uint8_t p = patternForKey(k);
        currentBraillePattern = p;
        brailleIsNumeric = (k >= '0' && k <= '9');
        if (k >= '0' && k <= '9') currentNumberPattern = patternForNumberDisplay(k - '0');
        else currentNumberPattern = 0x00;
        updateOutputs();
      } else {
        if (k >= '0' && k <= '9') {
          currentNumberPattern = patternForNumberDisplay(k - '0');
          updateOutputs();
        }
      }
    }
  } else {
    digitalWrite(KEY_INDICATOR_PIN, LOW);
    if (lastKey != 0) lastKey = 0;
  }

  // Speaker: buzz only when the display was explicitly set to numeric mode
  bool shouldBuzz = brailleIsNumeric && brailleRepresentsDigit();
  if (shouldBuzz && !speakerOn) {
    tone(SPEAKER_PIN, 1000); // continuous tone while numeric
    speakerOn = true;
  } else if (!shouldBuzz && speakerOn) {
    noTone(SPEAKER_PIN);
    speakerOn = false;
  }

  delay(10);
}

/* -------------------------
   Diagnostic helper
   ------------------------- */
void performSingleBitTest() {
  for (uint8_t bit = 0; bit < 6; ++bit) {
    uint8_t pattern = (1 << bit);
    currentBraillePattern = pattern;
    currentNumberPattern = 0x00;
    updateOutputs();
    delay(800);
  }
  currentBraillePattern = 0x00;
  currentNumberPattern = 0x00;
  updateOutputs();
  delay(300);
}

/* -------------------------
   Character / Key mappings
   ------------------------- */
uint8_t patternForChar(char c) {
  // accept numeric input by mapping keypad-style digits to braille letters:
  // '1'->'a', '2'->'b', ... '9'->'i', '0'->'j'
  if (c >= '1' && c <= '9') {
    c = 'a' + (c - '1');
  } else if (c == '0') {
    c = 'j';
  }

  switch (tolower((unsigned char)c)) {
    case 'a': return DOT1;
    case 'b': return DOT1|DOT2;
    case 'c': return DOT1|DOT4;
    case 'd': return DOT1|DOT4|DOT5;
    case 'e': return DOT1|DOT5;
    case 'f': return DOT1|DOT2|DOT4;
    case 'g': return DOT1|DOT2|DOT4|DOT5;
    case 'h': return DOT1|DOT2|DOT5;
    case 'i': return DOT2|DOT4;
    case 'j': return DOT2|DOT4|DOT5;
    case 'k': return DOT1|DOT3;
    case 'l': return DOT1|DOT2|DOT3;
    case 'm': return DOT1|DOT3|DOT4;
    case 'n': return DOT1|DOT3|DOT4|DOT5;
    case 'o': return DOT1|DOT3|DOT5;
    case 'p': return DOT1|DOT2|DOT3|DOT4;
    case 'q': return DOT1|DOT2|DOT3|DOT4|DOT5;
    case 'r': return DOT1|DOT2|DOT3|DOT5;
    case 's': return DOT2|DOT3|DOT4;
    case 't': return DOT2|DOT3|DOT4|DOT5;
    case 'u': return DOT1|DOT3|DOT6;
    case 'v': return DOT1|DOT2|DOT3|DOT6;
    case 'w': return DOT2|DOT4|DOT5|DOT6;
    case 'x': return DOT1|DOT3|DOT4|DOT6;
    case 'y': return DOT1|DOT3|DOT4|DOT5|DOT6;
    case 'z': return DOT1|DOT3|DOT5|DOT6;
    default:  return 0xFF;
  }
}

uint8_t patternForKey(char k) {
  if (k >= '1' && k <= '9') {
    char letter = 'a' + (k - '1');
    return patternForChar(letter);
  }
  if (k == '0') return patternForChar('j');
  if (k == '*') return DOT2 | DOT3 | DOT5 | DOT6;
  if (k == '#') return DOT3 | DOT4;
  return 0x00;
}

/* -------------------------
   Output update (shift registers)
   ------------------------- */
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
  shiftOut(dataPin, clockPin, SHIFT_ORDER, outNumber);
  shiftOut(dataPin, clockPin, SHIFT_ORDER, outBraille);
  digitalWrite(latchPin, HIGH);
}

/* -------------------------
   Encoder ISR
   ------------------------- */
void encoderISR() {
  uint8_t a = digitalRead(ENC_PIN_A);
  uint8_t b = digitalRead(ENC_PIN_B);
  if (a == b) {
    if (encoderPos < ENC_MAX) ++encoderPos;
  } else {
    if (encoderPos > ENC_MIN) --encoderPos;
  }
  encoderMoved = true;
}

/* -------------------------
   Keypad scanning
   ------------------------- */
char keypadScan() {
  for (uint8_t r = 0; r < KEYPAD_ROWS; ++r) {
    digitalWrite(KEYPAD_ROW_PINS[r], LOW);
    delayMicroseconds(5);
    for (uint8_t c = 0; c < KEYPAD_COLS; ++c) {
      if (digitalRead(KEYPAD_COL_PINS[c]) == LOW) {
        delay(12);
        bool still = (digitalRead(KEYPAD_COL_PINS[c]) == LOW);
        digitalWrite(KEYPAD_ROW_PINS[r], HIGH);
        if (still) {
          uint8_t idx = r * KEYPAD_COLS + c;
          if (idx < sizeof(KEYPAD_MAP)) return KEYPAD_MAP[idx];
          return 0;
        }
      }
    }
    digitalWrite(KEYPAD_ROW_PINS[r], HIGH);
  }
  return 0;
}

/* -------------------------
   7-segment digit mapping
   ------------------------- */
uint8_t patternForNumberDisplay(int n) {
  if (n < 0) n = 0;
  if (n > 9) n = 9;
  static const uint8_t segDigits[10] = {
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

/* -------------------------
   Numeric detection helper
   ------------------------- */
// true if currentBraillePattern matches the braille patterns used for digits
// (digits 1..9,0 share patterns with letters 'a'..'j')
bool brailleRepresentsDigit() {
  for (char c = 'a'; c <= 'j'; ++c) {
    if (currentBraillePattern == patternForChar(c)) return true;
  }
  return false;
}