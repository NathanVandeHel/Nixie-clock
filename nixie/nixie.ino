#include <Wire.h> 
#include <ds3231.h>


#define SR_OUTPUT_EN    2 //shift registers output enable (-OE), active low
#define SR_MASTER_CLEAR 6 //shift registers master clear (-SRCLR), active low
#define SR_LOCK         1 //shift registers storage clock (RCLK)
#define SR_CLOCK        0 //shift registers clock (SRCLK)
#define SR_DATA         3 //first shift register serial input (SER)

#define LEFT_BUTTON     7
#define RIGHT_BUTTON    8

// SN74HC595N pinout :
//    ┌───────┐
//  QB│1  U 16│VCC          QA - QH  : outputs
//  QC│2    15│QA           SER      : serial input
//  QD│3    14│SER          -OE      : output enable, active low
//  QE│4    13│-OE          RCLK     : storage register clock
//  QF│5    12│RCLK         SRCLK    : shift register clock
//  QG│6    11│SRCLK        -SRCLR   : master clear, active low
//  QH│7    10│-SRCLR       QH'      : serial out
// GND│8     9│QH'
//    └───────┘

// The shift registers are connected in series : the serial output of the first one is connected to the serial input of the second one.
// The shift registers and the tube drivers are connected as follow :
// tube number          :    Tube 1         Tube 2        Tube 3        Tube 4
// tube function        :   hour ten       hour one     minute ten    minute one
// tube driver entry    :  A  D  B  C     A  D  B  C    A  D  B  C    A  D  B  C
// SR number and output : 1E 1F 1G 1H    1A 1D 1C 1B   2E 2F 2G 2H   2A 2D 2C 2B
// sending sequence     : 12 11 10 9     16 13 14 15   4  3  2  1    8  5  6  7

// The value send being shifted each time, the last output of the line (SR2, output H) needs to be send in first
// The connection sequence of the ten tube drivers (ADBC) is not the same as the connection sequence of the one tube drivers (ACBD). 
// This means that for the same digit output, the four bits sequence send to the corresponding SR output will not be the same. 
// The truth tables bellow give the bit sequences to send to the cooresponding SR outputs to display the wanted digit :

                            //ADBC driver input
uint8_t truthTableTen[11] = {0b0000,    // digit 0
                              0b1000,   // digit 1
                              0b0010,   // digit 2
                              0b1010,   // digit 3
                              0b0001,   // digit 4
                              0b1001,   // digit 5
                              0b0011,   // digit 6
                              0b1011,   // digit 7
                              0b0100,   // digit 8
                              0b1100,   // digit 9
                              0b1111};  // no digit, for leading zero hiding rule

                            //ACBD driver input
uint8_t truthTableOne[11] = {0b0000,    // digit 0
                              0b1000,   // digit 1
                              0b0010,   // digit 2
                              0b1010,   // digit 3
                              0b0100,   // digit 4
                              0b1100,   // digit 5
                              0b0110,   // digit 6
                              0b1110,   // digit 7
                              0b0001,   // digit 8
                              0b1001,   // digit 9
                              0b1111};  // no digit, for leading zero hiding rule

struct ts t;

bool RULE_HIDE_LEADING_ZERO_HOURS = true; // hide the leading "0" when the hour is only one digit
bool RULE_TWELVE_HOURS = false; // displays the hour in 12 or 24 hours format

bool FLICKER_VIEW = false;
uint8_t FLICKER_SPEED = 500;
uint8_t FLICKER_CHOICE = 0;

bool STOP_UPDATING_TIME = false;

uint8_t STATE = 0;
bool LEFT_BUTTON_PRESSED = false;
bool RIGHT_BUTTON_PRESSED = false;
unsigned long LEFT_BUTTON_BEGIN_PRESSED_TIME = 0;
unsigned long RIGHT_BUTTON_BEGIN_PRESSED_TIME = 0;

uint8_t hourTen = 0;
uint8_t hourOne = 0;
uint8_t minuteTen = 0;
uint8_t minuteOne = 0;

uint8_t changedHourTen = 0;
uint8_t changedHourOne = 0;
uint8_t changedMinuteTen = 0;
uint8_t changedMinuteOne = 0;

void setup() 
{
  pinMode(SR_OUTPUT_EN, OUTPUT);
  pinMode(SR_MASTER_CLEAR, OUTPUT);
  pinMode(SR_LOCK, OUTPUT);
  pinMode(SR_CLOCK, OUTPUT);
  pinMode(SR_DATA, OUTPUT);

  pinMode(LEFT_BUTTON, INPUT_PULLUP);
  pinMode(RIGHT_BUTTON, INPUT_PULLUP);

  digitalWrite(SR_OUTPUT_EN, LOW);
  digitalWrite(SR_MASTER_CLEAR, HIGH);

  Serial.begin(9600);
  Wire.begin();
  DS3231_init(DS3231_CONTROL_INTCN);
}

void loop() 
{
  
  if (!STOP_UPDATING_TIME)
  {
    // read the RTC
    DS3231_get(&t);

    // separating the tens and ones of the hours and minutes
    separateHours();
    separateMinutes();
  }
  else
  {
    hourTen = changedHourTen;
    hourOne = changedHourOne;
    minuteTen = changedMinuteTen;
    minuteOne = changedMinuteOne;
  }

   // update the state
  updateState();

  // update the flicker view
  updateView();
  
  // write the numbers
  writeNumbers(hourTen, hourOne, minuteTen, minuteOne);
}

// 
void writeNumbers(uint8_t tube1, uint8_t tube2,  uint8_t tube3, uint8_t tube4)
{
  uint16_t value = 0;

  value = ((truthTableOne[tube2] << 12) & 0xF000) +  //tube 2
          ((truthTableTen[tube1] << 8) & 0X0F00) + //tube 1
          ((truthTableOne[tube4] << 4) & 0x00F0) + //tube 4
          (truthTableTen[tube3] & 0x000F); //tube 3

  // writing the bits on the shift registers
  digitalWrite(SR_LOCK, LOW);
  
  for (int i = 0; i < 16; i++)
  {
    digitalWrite(SR_CLOCK, LOW);
    digitalWrite(SR_DATA, value & (0x01 << i));
    digitalWrite(SR_CLOCK, HIGH);
  }
  
  digitalWrite(SR_LOCK, HIGH);
}

// separate the hour red of the RTC module on tens and ones. Adjust the hour in 12h format if needed
void separateHours()
{
  hourTen = RULE_TWELVE_HOURS && t.hour > 12 ? ((t.hour - 12) / 10) : (t.hour / 10);
  if (RULE_HIDE_LEADING_ZERO_HOURS && hourTen == 0)
    hourTen = 10; // if the hide leadin zero urle is applicable, chage the hour ten to 10 to correspond to the no digit output in the truth table 

  hourOne = RULE_TWELVE_HOURS && t.hour > 12 ? ((t.hour - 12) % 10) : (t.hour % 10);
}

// separate the minute red of the RTC module on tens and ones.
void separateMinutes()
{
  minuteTen = t.min / 10;
  minuteOne = t.min % 10;
}


void updateView()
{
  if (FLICKER_VIEW)
  {   
    if ((millis() / FLICKER_SPEED) % 2 == 0)
    {
      switch (FLICKER_CHOICE)
      {
        Serial.println("flicker choice");
        case 0:
          hourTen = 10;
          hourOne = 10;
          minuteTen = 10;
          minuteOne = 10;
          break;
        case 1:
          hourTen = 10;
          break;
        case 2:
          hourOne = 10;
          break;
        case 3:
          minuteTen = 10;
          break;
        case 4:
          minuteOne = 10;
          break;
        case 5:
          hourTen = 10;
          hourOne = 10;
          break;
        case 6:
          minuteTen = 10;
          minuteOne = 10;
          break;
      }
    }
  }
}

void updateState()
{
  switch(STATE)
  {
    case 0: // normal state
    Serial.println("normal state");

      if (digitalRead(LEFT_BUTTON) == LOW)
      {
        LEFT_BUTTON_PRESSED = true;
        LEFT_BUTTON_BEGIN_PRESSED_TIME = millis();
        STATE = 1;
      }
      if (digitalRead(RIGHT_BUTTON) == LOW)
      {
        RIGHT_BUTTON_PRESSED = true;
        RIGHT_BUTTON_BEGIN_PRESSED_TIME = millis();
        STATE = 2;
      }
      break;

    case 1: // left button is being pressed
      Serial.println("left button being pressed");

      if (digitalRead(LEFT_BUTTON) == LOW && millis() - LEFT_BUTTON_BEGIN_PRESSED_TIME > 2000)
      {
        STATE = 3;
        FLICKER_VIEW = true;
        FLICKER_CHOICE = 0;
      }
      else if (digitalRead(LEFT_BUTTON) == HIGH)
      {
        LEFT_BUTTON_PRESSED = false;
        STATE = 0;
      }
      break;

    case 2: // right button is being pressed
      Serial.println("right button being pressed");

      if (digitalRead(RIGHT_BUTTON) == LOW && millis() - RIGHT_BUTTON_BEGIN_PRESSED_TIME > 2000)
      {
        STATE = 3;
        FLICKER_VIEW = true;
        FLICKER_CHOICE = 0;
      }
      else if (digitalRead(RIGHT_BUTTON) == HIGH)
      {
        RIGHT_BUTTON_PRESSED = false;
        STATE = 0;
      }
      break;

    case 3: // waiting for the button to be released
      Serial.println("waiting for the button to be released");

      if (digitalRead(LEFT_BUTTON) == HIGH && digitalRead(RIGHT_BUTTON) == HIGH)
      {
        LEFT_BUTTON_PRESSED = false;
        RIGHT_BUTTON_PRESSED = false;

        STATE = 4;

        FLICKER_CHOICE = 5;
      }

      delay(10);
      break;

    case 4: // 12 or 24 hours update, twelve choice
      Serial.println("12 or 24 hours update, 12 choice");

      //overiding the numbers to choose from "12" or "24"
      hourTen = 1;
      hourOne = 2;
      minuteTen = 2;
      minuteOne = 4;

      if (digitalRead(RIGHT_BUTTON) == LOW)
      {
        RIGHT_BUTTON_PRESSED = true;
        LEFT_BUTTON_PRESSED = false;
      }
      else
      {
        if (RIGHT_BUTTON_PRESSED == true)
        {
          RIGHT_BUTTON_PRESSED = false;

          STATE = 5;
          FLICKER_CHOICE = 6;
        }
      }

      if(digitalRead(LEFT_BUTTON) == LOW)
      {
        LEFT_BUTTON_PRESSED = true;
        RIGHT_BUTTON_PRESSED = false;
      }
      else
      {
        if (LEFT_BUTTON_PRESSED == true)
        {
          LEFT_BUTTON_PRESSED = false;

          RULE_TWELVE_HOURS = true;

          //preparing for next state : setting the hour ten
          STATE = 6;
          FLICKER_CHOICE = 1;
          RULE_HIDE_LEADING_ZERO_HOURS = false;
          STOP_UPDATING_TIME = true;

          changedHourTen = 0;
          changedHourOne = 0;
          changedMinuteTen = 0;
          changedMinuteOne = 0;
        }
      }

      delay(10);
      break;

    case 5: // 12 or 24 hours update, 24 choice
      Serial.println("12 or 24 hours update, 24 choice");

      //overiding the numbers to choose from "12" or "24"
      hourTen = 1;
      hourOne = 2;
      minuteTen = 2;
      minuteOne = 4;

      if (digitalRead(RIGHT_BUTTON) == LOW)
      {
        RIGHT_BUTTON_PRESSED = true;
        LEFT_BUTTON_PRESSED = false;
      }
      else
      {
        if (RIGHT_BUTTON_PRESSED == true)
        {
          RIGHT_BUTTON_PRESSED = false;

          STATE = 4;
          FLICKER_CHOICE = 5;
        }
      }

      if(digitalRead(LEFT_BUTTON) == LOW)
      {
        LEFT_BUTTON_PRESSED = true;
        RIGHT_BUTTON_PRESSED = false;
      }
      else
      {
        if (LEFT_BUTTON_PRESSED == true)
        {
          LEFT_BUTTON_PRESSED = false;

          RULE_TWELVE_HOURS = false;

          //preparing for next state : setting the hour ten
          STATE = 6;
          FLICKER_CHOICE = 1;
          RULE_HIDE_LEADING_ZERO_HOURS = false;
          STOP_UPDATING_TIME = true;

          changedHourTen = 0;
          changedHourOne = 0;
          changedMinuteTen = 0;
          changedMinuteOne = 0;
        }
      }

      delay(10);
      break;

    case 6: // hour ten update
      Serial.println("hour ten update");



      if (digitalRead(RIGHT_BUTTON) == LOW)
      {
        RIGHT_BUTTON_PRESSED = true;
        LEFT_BUTTON_PRESSED = false;
      }
      else
      {
        if (RIGHT_BUTTON_PRESSED == true)
        {
          RIGHT_BUTTON_PRESSED = false;

          STATE = 7;
          FLICKER_CHOICE = 2;
        }
      }

      if (digitalRead(LEFT_BUTTON) == LOW)
      {
        LEFT_BUTTON_PRESSED = true;
        RIGHT_BUTTON_PRESSED = false;
      }
      else
      {
        if (LEFT_BUTTON_PRESSED == true)
        {
          LEFT_BUTTON_PRESSED = false;

          if(RULE_TWELVE_HOURS)
          {
            if(changedHourTen == 1)
            {
              changedHourTen = 0;
            }
            else
            {
              changedHourTen++;
            }
          }
          else
          {
            if(changedHourTen == 2)
            {
              changedHourTen = 0;
            }
            else
            {
              changedHourTen++;
            }
          }
        }
      }

      delay(10);
      break;

    case 7: // hour one update
      Serial.println("hour one update");
      if (digitalRead(RIGHT_BUTTON) == LOW)
      {
        RIGHT_BUTTON_PRESSED = true;
        LEFT_BUTTON_PRESSED = false;
      }
      else
      {
        if (RIGHT_BUTTON_PRESSED == true)
        {
          RIGHT_BUTTON_PRESSED = false;

          STATE = 8;
          FLICKER_CHOICE = 3;
        }
      }

      if (digitalRead(LEFT_BUTTON) == LOW)
      {
        LEFT_BUTTON_PRESSED = true;
        RIGHT_BUTTON_PRESSED = false;
      }
      else
      {
        if (LEFT_BUTTON_PRESSED == true)
        {
          LEFT_BUTTON_PRESSED = false;

          if(changedHourTen == 2)
          {
            if(changedHourOne == 3)
            {
              changedHourOne = 0;
            }
            else
            {
              changedHourOne++;
            }
          }
          else if (changedHourTen == 1)
          {
            if (RULE_TWELVE_HOURS)
            {
              if(changedHourOne == 2)
              {
                changedHourOne = 0;
              }
              else
              {
                changedHourOne++;
              }
            }
            else
            {
              if(changedHourOne == 9)
              {
                changedHourOne = 0;
              }
              else
              {
                changedHourOne++;
              }
            }
          }
          else
          {
            if(changedHourOne == 9)
              {
                changedHourOne = 0;
              }
              else
              {
                changedHourOne++;
              }
          }
        }
      }

      delay(10);
      break;

    case 8: // minute ten update
      Serial.println("minute ten update");
      if (digitalRead(RIGHT_BUTTON) == LOW)
      {
        RIGHT_BUTTON_PRESSED = true;
        LEFT_BUTTON_PRESSED = false;
      }
      else
      {
        if (RIGHT_BUTTON_PRESSED == true)
        {
          RIGHT_BUTTON_PRESSED = false;

          STATE = 9;
          FLICKER_CHOICE = 4;
        }
      }

      if (digitalRead(LEFT_BUTTON) == LOW)
      {
        LEFT_BUTTON_PRESSED = true;
        RIGHT_BUTTON_PRESSED = false;
      }
      else
      {
        if (LEFT_BUTTON_PRESSED == true)
        {
          LEFT_BUTTON_PRESSED = false;

          if (changedMinuteTen == 5)
          {
            changedMinuteTen = 0;
          }
          else
          {
            changedMinuteTen++;
          }
        }
      }

      delay(10);
      break;

    case 9: // minute one update
      Serial.println("minute one update");
      if (digitalRead(RIGHT_BUTTON) == LOW)
      {
        RIGHT_BUTTON_PRESSED = true;
        LEFT_BUTTON_PRESSED = false;
      }
      else
      {
        if (RIGHT_BUTTON_PRESSED == true)
        {
          RIGHT_BUTTON_PRESSED = false;

          //finishing seting hour job
          updateClock();
          STOP_UPDATING_TIME = false;

          //preparing for next state
          STATE = 10;
          FLICKER_CHOICE = 5;
        }
      }

      if (digitalRead(LEFT_BUTTON) == LOW)
      {
        LEFT_BUTTON_PRESSED = true;
        RIGHT_BUTTON_PRESSED = false;
      }
      else
      {
        if (LEFT_BUTTON_PRESSED == true)
        {
          LEFT_BUTTON_PRESSED = false;

          if(changedMinuteOne == 9)
          {
            changedMinuteOne = 0;
          }
          else
          {
            changedMinuteOne++;
          }
        }
      }

      delay(10);
      break;

    case 10: // leading zero rule, no hide choice
      Serial.println("leading zero rule, no hide choice");

      //overiding the numbers to choose from "01" or "X1"
      hourTen = 0;
      hourOne = 1;
      minuteTen = 10;
      minuteOne = 1;

      if (digitalRead(RIGHT_BUTTON) == LOW)
      {
        RIGHT_BUTTON_PRESSED = true;
        LEFT_BUTTON_PRESSED = false;
      }
      else
      {
        if (RIGHT_BUTTON_PRESSED == true)
        {
          RIGHT_BUTTON_PRESSED = false;

          STATE = 11;
          FLICKER_CHOICE = 6;
        }
      }

      if(digitalRead(LEFT_BUTTON) == LOW)
      {
        LEFT_BUTTON_PRESSED = true;
        RIGHT_BUTTON_PRESSED = false;
      }
      else
      {
        if (LEFT_BUTTON_PRESSED == true)
        {
          LEFT_BUTTON_PRESSED = false;

          RULE_HIDE_LEADING_ZERO_HOURS = false;

          STATE = 0;
          FLICKER_CHOICE = 0;
          FLICKER_VIEW = false;
          
        }
      }

      delay(10);
      break;

    case 11: // leading zero rule, hide choice
      Serial.println("leading zero rule, hide choice");

      //overiding the numbers to choose from "01" or "X1"
      hourTen = 0;
      hourOne = 1;
      minuteTen = 10;
      minuteOne = 1;

      if (digitalRead(RIGHT_BUTTON) == LOW)
      {
        RIGHT_BUTTON_PRESSED = true;
        LEFT_BUTTON_PRESSED = false;
      }
      else
      {
        if (RIGHT_BUTTON_PRESSED == true)
        {
          RIGHT_BUTTON_PRESSED = false;

          STATE = 10;
          FLICKER_CHOICE = 5;
        }
      }

      if(digitalRead(LEFT_BUTTON) == LOW)
      {
        LEFT_BUTTON_PRESSED = true;
        RIGHT_BUTTON_PRESSED = false;
      }
      else
      {
        if (LEFT_BUTTON_PRESSED == true)
        {
          LEFT_BUTTON_PRESSED = false;

          RULE_HIDE_LEADING_ZERO_HOURS = true;

          STATE = 0;
          FLICKER_CHOICE = 0;
          FLICKER_VIEW = false;
        }
      }

      delay(10);
      break;
  }
}

void updateClock()
{
  t.hour = changedHourTen * 10 + changedHourOne;
  t.min = changedMinuteTen * 10 + changedMinuteOne;
  DS3231_set(t);
}
