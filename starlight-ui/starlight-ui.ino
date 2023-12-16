// Arduino-based Starlight UI
// Tom Browne - 8th December 2023
// Uses the "1602 keypad shield" which provides 5 buttons and a 16x2 text LCD
//
// Connection to the Starlight is via. serial at 9600-8-N-1
//
// Action commands
// 'b' bipolar mode
// 'o' 1704 mode
// 'w' USB
// 'z' Squeezebox streaming
// 'd' Disc (CD)
// 't' Tone
// 'n' Noise
// 'p' Play
// 's' Stop
// 'e' Eject
// 'c' Close tray
// '>' Next track
// '<' Prev track
// '.' FF
// ',' RW
// '/' Pause
// 'q' Quit (shuts down the transport)
// '1' to '0' Track 1 to 10 direct
// 
// Info commands
// 'T' Track info
// 'M' Mode (play, stop, etc.)
// 'S' Source info (disc, usb, strm)
// 'V' Version of software
// 'O' Output Mode
// 'F' Title/Filename of current track
// 'R' Sampling rate info (source -> output)
// 
// Note: 192K playback will not work as the Starlights are designed to stay at 176.4K.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

// https://www.arduino.cc/reference/en/libraries/liquidcrystal/
#include <LiquidCrystal.h>

// https://docs.arduino.cc/learn/built-in-libraries/software-serial
#include <SoftwareSerial.h>

const int pin_RS = 8; 
const int pin_EN = 9; 
const int pin_D4 = 4; 
const int pin_D5 = 5; 
const int pin_D6 = 6; 
const int pin_D7 = 7; 
const int pin_BL = 10; 

LiquidCrystal lcd( pin_RS,  pin_EN,  pin_D4,  pin_D5,  pin_D6,  pin_D7);
const int lcdWidth = 16;
const int lcdHeight = 2;

char lineText[lcdWidth+1];
char modeText[10]; // e.g. play
char sourceText[10]; // e.g. stream
char timeText[20]; // e.g. 01/02  00:10
char ratesText[20]; // e.g. 44100

enum
{
  key_none = 0,
  key_up,
  key_down,
  key_left,
  key_right,
  key_select
} keys;

int connected = 0;
int last_key = key_none;
int key_repeated = 0;
int source = 1; // CD=0, streaming=1
int playing = 0;
int stopped = 0;
int readystream = 0;

unsigned long firstEject = 0;

#define rxPin 12
#define txPin 11

// Using SoftwareSerial library, which appears slightly flaky at receiving
// RX: 12, TX: 11, Invert logic (for direct connection of RX via. 10K series resistor to GD75232 TX)
// Note: Omitting this 10K resistor *will* blow up the Arduino, as RS-232 levels are +/-12V
//       ... the 10K resistor is enough to reduce max current to ~1mA in the ATMega ESD diodes
//
// At D9 PC end:
// pin 2: PC receive (direct connect)
// pin 3: PC transmit (*must* go through 10kohm resistor to avoid damage to uC)
// pin 5: GND
SoftwareSerial ser(rxPin, txPin, true); 


void clearBuf()
{
  int i;
  for(i=0; i < lcdWidth; i++)
  {
    lineText[i] = ' ';
  }
  lineText[i] = 0;
}

void setup()
{
  lcd.begin(lcdWidth, lcdHeight);
  lcd.setCursor(0,0);

  lcd.print("Starlight UI 0.1");
  lcd.setCursor(0,1);
  lcd.print("Waiting...");

  // Define pin modes for TX and RX
  pinMode(rxPin, INPUT);
  pinMode(txPin, OUTPUT); 

  ser.begin(9600);
  delay(500);
  ser.flush();

  modeText[0] = 0;
  sourceText[0] = 0;
}


int getResponse(char cmd, char *buf, int expected)
{
  int gotBytes = 0;
  int outLen = 0;
  int sleepTime = expected * 4;

  ser.flush();
  ser.write(cmd);
  delay(100); // SoftwareSerial seems buggy, prefers a wait while incoming data
  char val = 32;
  char termByte = 0;
  
  gotBytes = ser.available();
  while(ser.available())
  {
    val = ser.read();
    if(val != 0x0a && val != 13)
    {
      *buf++ = val;
      outLen++;
    }
    else
    {
      termByte++;
    }

    gotBytes--;
  }

  *buf++ = 0x0; // terminate buffer

  return outLen;
}


int getKey()
{
  int x;
  x = analogRead (0);

  if (x < 60)
  {
    return key_right;
  }
  else if(x < 200)
  {
    return key_up;
  }
  else if(x < 400)
  {
    return key_down;
  }
  else if(x < 620)
  {
    return key_left;
  }
  else if(x < 800)
  {
    return key_select;
  }

  return key_none;
}


void loop()
{
  int gotBytes;

  if(!connected)
  {
    gotBytes = getResponse('V', lineText, 14);

    if(gotBytes)
    {
      lcd.setCursor(0,1);
      lcd.print(lineText);
      connected = 1;

      delay(1000);
      readystream = 1;
    }
    else
    {
      return;
    }
  }

  // get current playback mode
  int modeBytes = getResponse('M', modeText, 6);
  if(modeBytes > 2) // should be a minimum of 2 bytes
  {
    stopped = (modeText[0] != 'p') ? 1 : 0;
  }

  if(readystream == 1 && stopped)
  {
    // go to streaming mode by default and hit play
    ser.write('z');
    delay(10);
    ser.write('p');
    delay(100); // otherwise STRM play gets reversed on startup
    readystream = 0;
  }

  int sourceBytes = getResponse('S', sourceText, 6);
  int timeBytes = getResponse('T', timeText, 14);
  int rateBytes = getResponse('R', ratesText, 14);
  if(rateBytes > 6) // only have room for input rate
  {
      if(ratesText[5] == 'H') // e.g. 44100
      {
        rateBytes = 5;
        ratesText[5] = 0;
      }
      else if(ratesText[6] == 'H') // e.g. 192K
      {
        rateBytes = 6;
        ratesText[6] = 0;
      }
  }
  else
  {
    // invalid?
    rateBytes = 0;
    ratesText[0] = 0;
  }

  int key = getKey();
  if(key != last_key)
  {
    if(key == key_select) // source select
    {
      key_repeated = 0;

      if(source == 0)
      {
        ser.write('z'); // stream
        delay(10);
        ser.write('s'); // stop
        delay(10);
        ser.write('p'); //play
        source = 1;
      }
      else if(source == 1)
      {
        ser.write('d'); // CD player
        delay(10);
        ser.write('s'); // stop
        source = 0;
      }
    }
    else if(key == key_up) // prev track
    {
      ser.write('<');
      delay(10);
    }
    else if(key == key_down) // next track
    {
      ser.write('>');
      delay(10);
    }
    else if(key == key_left) // stop
    {
      ser.write('s');
      delay(10);
    }
    else if(key == key_right) // pause/play
    {
      ser.write((stopped) ? 'p' : '/');
      delay(10);
    }
  }
  else
  {
    key_repeated++;

    if(key_repeated == 5 && key == key_select)
    {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Shutting down");
      ser.write('q');
      delay(20000); // should be shut down after 20 secs!
    }
    else if(key == key_left && source == 0)
    {
        ser.write('e'); // CD eject probably wanted
        delay(10);
    }
  }
  last_key = key;

  // update display

  // top line - "strm play"
  lcd.setCursor(0,0);
  lcd.print(sourceText);
  lcd.print(" ");
  lcd.print(modeText);
  lcd.print(" ");
  lcd.print(ratesText);
  int bytesWritten = sourceBytes+1+modeBytes+1+rateBytes;
  while(bytesWritten < lcdWidth)
  {
    lcd.print(" ");
    bytesWritten++;
  }
  
  // second line - timing
  lcd.setCursor(0,1);
  lcd.print(timeText);
  bytesWritten = timeBytes;
  while(bytesWritten < lcdWidth)
  {
    lcd.print(" ");
    bytesWritten++;
  }

  delay(10);
}
