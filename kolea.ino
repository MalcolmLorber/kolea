/**
   StenoFW is a firmware for Stenoboard keyboards.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.

   Copyright 2014 Emanuele Caruso. See LICENSE.txt for details.
*/

/**
   Matrix modified for the Kolea keyboard.
*/
#include <EEPROM.h>

#define ROWS 4
#define COLS 11
#define PROTOCOL_ADDR 11
#define DELAY_ADDR 12
#define DEBUG_MODE 0
/* The following matrix is shown here for reference only.
  char keys[ROWS][COLS] = {
    {' ', '2', '3', '4', '5', ' ', '7', '8', '9', '0', ' '},
    {' ', 'q', 'w', 'e', 'r', 't', 'u', 'i', 'o', 'p', '['},
    {' ', 'a', 's', 'd', 'f', 'g', 'j', 'k', 'l', ';', '\''},
    {' ', ' ', ' ', 'c', 'v', ' ', 'n', 'm', ' ', ' ', ' '}
  };*/

// Configuration variables
int rowPins[ROWS] = {4, 5, 6, 7};
int colPins[COLS] = {8, 9, 10, 11, 12, 14, 15, 16, 18, 19, 20};

// Keyboard state variables
unsigned long debounceMillis = 20;
bool inProgress = false;
bool currentChord[ROWS][COLS];
bool keyReadings[ROWS][COLS];
bool realKeys[ROWS][COLS];
unsigned long debouncingMillis[ROWS][COLS];

// Protocol state
#define GEMINI 0
#define TXBOLT 1
#define NKRO 2
int protocol = GEMINI;

// This is called when the keyboard is connected
void setup() {
  Keyboard.begin();
  Serial.begin(9600);
  protocol = EEPROM.read(PROTOCOL_ADDR);
  if (protocol > 2 || protocol < 0) {
    protocol = 0;
    EEPROM.write(PROTOCOL_ADDR, 0);
  }
  debounceMillis = EEPROM.read(DELAY_ADDR);
  if (debounceMillis > 35 || debounceMillis < 0) {
    debounceMillis = 20;
    EEPROM.write(DELAY_ADDR, 20);
  }
  for (int i = 0; i < COLS; i++)
    pinMode(colPins[i], INPUT_PULLUP);
  for (int i = 0; i < ROWS; i++) {
    pinMode(rowPins[i], OUTPUT);
    digitalWrite(rowPins[i], HIGH);
  }
  clearMatrix(currentChord);
  clearMatrix(keyReadings);
  clearMatrix(realKeys);
}

// Read key states and handle all chord events
void loop() {
  unsigned long curTime = millis();
  readKeys();

  bool anyPressed = false;

  for (int i = 0; i < ROWS; i++) {
    for (int j = 0; j < COLS; j++) {
      //we see if state has changed AND been given time to debounce
      if ((keyReadings[i][j] != currentChord[i][j]) && (curTime - debouncingMillis[i][j] >= debounceMillis)) {
        //add the key to the chord if it was pressed, and start a chord if not already
        if (keyReadings[i][j]) {
          currentChord[i][j] = true;
          inProgress = true;
        }
        //update the debounced key reading and set the last debounced time for that key
        realKeys[i][j] = keyReadings[i][j];
        debouncingMillis[i][j] = curTime;
      }
      //see if any DEBOUNCED keys are actually being pressed
      anyPressed |= realKeys[i][j];
    }
  }
  //if no keys are pressed and a chord was started, we send the chord
  if (!anyPressed && inProgress) {
    sendChord();
    clearMatrix(currentChord);
    inProgress = false;
  }
}

// Set all values of the passed matrix to 0/false
void clearMatrix(bool m[][COLS]) {
  memset(m, 0, sizeof(m[0][0])*ROWS * COLS);
}

// Read all keys
void readKeys() {
  for (int i = 0; i < ROWS; i++) {
    digitalWrite(rowPins[i], LOW);
    for (int j = 0; j < COLS; j++)
      keyReadings[i][j] = !digitalRead(colPins[j]);
    digitalWrite(rowPins[i], HIGH);
  }
}

// Send current chord using NKRO Keyboard emulation
void sendChordNkro() {
  // QWERTY mapping
  char qwertyMapping[ROWS][COLS] = {
    {' ', '2', '3', '4', '5', ' ', '7', '8', '9', '0', ' '},
    {' ', 'q', 'w', 'e', 'r', 't', 'u', 'i', 'o', 'p', '['},
    {' ', 'a', 's', 'd', 'f', 'g', 'j', 'k', 'l', ';', '\''},
    {' ', ' ', ' ', 'c', 'v', ' ', 'n', 'm', ' ', ' ', ' '}
  };
  int keyCounter = 0;
  char qwertyKeys[ROWS * COLS];

  // Calculate qwerty keys array using qwertyMappings[][]
  for (int i = 0; i < ROWS; i++) {
    for (int j = 0; j < COLS; j++) {
      if (currentChord[i][j]) {
        qwertyKeys[keyCounter] = qwertyMapping[i][j];
        keyCounter++;
      }
    }
  }
  // Emulate keyboard key presses
  for (int i = 0; i < keyCounter; i++) {
    if (qwertyKeys[i] != ' ') {
      Keyboard.press(qwertyKeys[i]);
    }
  }
  Keyboard.releaseAll();
}

// Send current chord over serial using the Gemini protocol.
void sendChordGemini() {
  // Initialize chord bytes
  byte chordBytes[] = {B10000000, B0, B0, B0, B0, B0};
  unsigned int geminiVals[ROWS][COLS] =
  { {0,  1,  1,  1,  1,  0,  1,  1,  1,  1,  0},
    {0, 64, 16,  4,  1,  8,  2, 64, 16,  4,  1},
    {0, 64,  8,  2, 64,  8,  1, 32,  8,  2,  1},
    {0,  0,  0, 32, 16,  0,  8,  4,  0,  0,  0}
  };

  unsigned int geminiByte[ROWS][COLS] =
  { {0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
    {0,  1,  1,  1,  1,  2,  3,  4,  4,  4,  4},
    {0,  1,  1,  1,  2,  2,  3,  4,  4,  4,  5},
    {0,  0,  0,  2,  2,  0,  3,  3,  0,  0,  0}
  };

  for (int i = 0; i < ROWS; i++) {
    for (int j = 0; j < COLS; j++) {
      if (currentChord[i][j]) {
        chordBytes[geminiByte[i][j]] |= geminiVals[i][j];
      }
    }
  }
  // Send chord bytes over serial
  for (int i = 0; i < 6; i++) {
    Serial.write(chordBytes[i]);
  }
}

void sendChordTxBolt() {
  byte chordBytes[] = {B0, B0, B0, B0};

  // TX Bolt uses a variable length packet. Only those bytes that have active
  // keys are sent. The header bytes indicate which keys are being sent. They
  // must be sent in order. It is a good idea to send a zero after every packet.
  // 00XXXXXX 01XXXXXX 10XXXXXX 110XXXXX
  //   HWPKTS   UE*OAR   GLBPRF    #ZDST

  unsigned int boltVals[ROWS][COLS] =
  { {0, 208, 208, 208, 208,   0, 208, 208, 208, 208,   0},
    {0,   1,   2,   8,  32,  72, 129, 132, 144, 193, 196},
    {0,   1,   4,  16,  65,  72, 130, 136, 160, 194, 200},
    {0,   0,   0,  66,  68,   0,  80,  96,   0,   0,   0}
  };
  unsigned int boltByte[ROWS][COLS] =
  { {0,   3,   3,   3,   3,   0,   3,   3,   3,   3,   0},
    {0,   0,   0,   0,   0,   1,   2,   2,   2,   3,   3},
    {0,   0,   0,   0,   1,   1,   2,   2,   2,   3,   3},
    {0,   0,   0,   1,   1,   0,   1,   1,   0,   0,   0}
  };

  for (int i = 0; i < ROWS; i++) {
    for (int j = 0; j < COLS; j++) {
      if (currentChord[i][j]) {
        chordBytes[boltByte[i][j]] |= boltVals[i][j];
      }
    }
  }
  for (int i = 0; i < 4; i++) {
    if (chordBytes[i])
      Serial.write(chordBytes[i]);
  }
  Serial.write(B0);
}

// Send the chord using the current protocol. If there are fn keys
// pressed, delegate to the corresponding function instead.
// In future versions, there should also be a way to handle fn keys presses before
// they are released, eg. for mouse emulation functionality or custom key presses.
void sendChord() {
  // If fn keys have been pressed, delegate to corresponding method and return
  if (currentChord[1][0] && currentChord[2][0]) {
    fn1fn2();
    return;
  } else if (currentChord[1][0]) {
    fn1();
    return;
  } else if (currentChord[2][0]) {
    fn2();
    return;
  }

  if (protocol == NKRO) {
    sendChordNkro();
  } else if (protocol == GEMINI) {
    sendChordGemini();
  } else {
    sendChordTxBolt();
  }
}

// Fn1 functions
//
// This function is called when "fn1" key has been pressed, but not "fn2".
// Tip: maybe it is better to avoid using "fn1" key alone in order to avoid
// accidental activation?
//
// Current functions:
//    PH-PB   ->   Set NKRO Keyboard emulation mode
//    PH-G   ->   Set Gemini PR protocol mode
//    PH-B   ->   Set TX Bolt protocol mode
void fn1() {
  // "PH" -> Set protocol
  if (currentChord[1][3] && currentChord[1][4]) {
    // "-PB" -> NKRO Keyboard
    if (currentChord[1][7] && currentChord[2][7]) {
      protocol = NKRO;
    }
    // "-G" -> Gemini PR
    else if (currentChord[2][8]) {
      protocol = GEMINI;
    }
    // "-B" -> TX Bolt
    else if (currentChord[2][7]) {
      protocol = TXBOLT;
    }
  }
}

// Fn2 functions
//
// This function is called when "fn2" key has been pressed, but not "fn1".
// Tip: maybe it is better to avoid using "fn2" key alone in order to avoid
// accidental activation?
//
// Current functions:
//    # - set delay based on number button pressed
//    A - if debug enabled, print some debug info to serial
void fn2() {
  if (currentChord[0][1])
    debounceMillis = 0;
  else if (currentChord[0][2])
    debounceMillis = 5;
  else if (currentChord[0][3])
    debounceMillis = 10;
  else if (currentChord[0][4])
    debounceMillis = 15;
  else if (currentChord[0][6])
    debounceMillis = 20;
  else if (currentChord[0][7])
    debounceMillis = 25;
  else if (currentChord[0][8])
    debounceMillis = 30;
  else if (currentChord[0][9])
    debounceMillis = 35;


  if (DEBUG_MODE && currentChord[3][3]) {
    Serial.println("protocol and delay: ");
    Serial.println(protocol);
    Serial.println(debounceMillis);
    Serial.println("in eeprom: ");
    Serial.println(EEPROM.read(PROTOCOL_ADDR));
    Serial.println(EEPROM.read(DELAY_ADDR));
  }
}

// Fn1-Fn2 functions
//
// This function is called when both "fn1" and "fn1" keys have been pressed.
//
// Current functions:
//   *-D   ->   Store Delay
//   *-Z   ->   Store Protocol
void fn1fn2() {
  if (currentChord[1][5] || currentChord[2][5]) {
    if (currentChord[1][10]) {
      EEPROM.write(DELAY_ADDR, debounceMillis);
    }
    if (currentChord[2][10]) {
      EEPROM.write(PROTOCOL_ADDR, protocol);
    }
  }
}
