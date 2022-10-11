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

bool RULE_HIDE_LEADING_ZERO_HOURS = true; // Hide the leading "0" when the hour is only one digit
bool RULE_TWELVE_HOURS = false; // Displays the hour in 12 or 24 hours format
uint8_t RULE_ECO_MODE_START_TIME = 0; // Start hour of the eco mode (tubes turned off)
uint8_t RULE_ECO_MODE_STOP_TIME = 0; // End hour of the eco mode (tubes turned bacl on)
bool RULE_ECO_MODE_START_TIME_SELECTED = true;
bool RULE_ECO_MODE_BYPASS = false; // Enable the user tu bypass the eco mode for one revolution

uint8_t STATE = 0;
uint8_t SHUTTER_MODE = 0;
bool STOP_UPDATING_TIME = false;

uint64_t FLICKER_SPEED = 500; // Flicker duration, in ms
uint64_t BUTTON_LONG_PRESS = 1000; // Time for a long press, in ms

bool LB_PRESSED = false; // Left Button Pressed
bool RB_PRESSED = false; // Right Button Pressed
bool LB_WAIT_FOR_RELEASE = false; // Wait for the release of the Left Button after a long press
bool RB_WAIT_FOR_RELEASE = false; // Wait for the release of the Right Button after a long press
unsigned long LB_PRESSED_TIME = 0; // Left Button Pressed Time
unsigned long RB_PRESSED_TIME = 0; // Right Button Pressed Time
bool LB_SP = false; // Left Button Short Press
bool RB_SP = false; // Right Button Short Press
bool LB_LP = false; // Left Button Long Press
bool RB_LP = false; // Right Button Long Press

// Always in 24 hours format
uint8_t hourTen = 0;
uint8_t hourOne = 0;
uint8_t minuteTen = 0;
uint8_t minuteOne = 0;

uint8_t ACTUAL_DIGIT_SELECTED = 0; // 0: hourTen, 1: hourOne, 2: minuteTen, 3: minuteOne
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

  setEcoTime();
}

void loop() 
{
  updateTime();

  checkButtons();

  updateStateMachine();

  updateView();
  
  updateTubes(hourTen, hourOne, minuteTen, minuteOne);

  reinitializeButtons();

  delay(100);
}

void updateTime()
{
  // read the RTC
  DS3231_get(&t);

  // separate the tens and ones of the hours and minutes
  // Also apply the different rules
  separateHours();
  separateMinutes();

  checkEcoTime();
}

// update the digits diplayed by the tubes
void updateTubes(uint8_t tube1, uint8_t tube2,  uint8_t tube3, uint8_t tube4)
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

// Separate the hour red of the RTC module on tens and ones. Adjust the hour in 12h format if needed
void separateHours()
{
  hourTen = RULE_TWELVE_HOURS && t.hour > 12 ? ((t.hour - 12) / 10) : (t.hour / 10);
  if (RULE_HIDE_LEADING_ZERO_HOURS && hourTen == 0) // if the hide leadin zero urle is applicable, chage the hour ten to 10 to correspond to the no digit output in the truth table 
    hourTen = 10;

  hourOne = RULE_TWELVE_HOURS && t.hour > 12 ? ((t.hour - 12) % 10) : (t.hour % 10);
}

// Separate the minute red of the RTC module on tens and ones.
void separateMinutes()
{
  minuteTen = t.min / 10;
  minuteOne = t.min % 10;
}

// Check if the year stored in the RTC as not changed. If so, set it back to the correct number
void checkEcoTime()
{
  if(RULE_ECO_MODE_START_TIME != t.mday || RULE_ECO_MODE_STOP_TIME != t.year - 2000 )
  {
    Serial.println("Eco times on the RTC does not correspond to saved");
    updateEcoTime();
  }
}

void setEcoTime()
{
  // read the RTC
  DS3231_get(&t);

  Serial.print("RTC day (eco mode time start) : ");
  Serial.println(t.mday);
  Serial.print("RTC year (eco mode time stop) : ");
  Serial.println(t.year);

  RULE_ECO_MODE_START_TIME = t.mday;
  RULE_ECO_MODE_STOP_TIME = t.year - 2000;

  Serial.print(" Eco mode times set : ");
  Serial.print(RULE_ECO_MODE_START_TIME);
  Serial.print(" ");
  Serial.println(RULE_ECO_MODE_STOP_TIME);
}

// Override the states of the tube in function of the mode we are currently in. Acts as a "shutter" in front of the tubes
void updateView()
{
  Serial.print("Shutter mode : ");
  Serial.println(SHUTTER_MODE);
  switch(SHUTTER_MODE)
  {
    case 0: // Standard mode
      break;

    case 1: // Eco mode, all tubes turned off
      hourTen = 10;
      hourOne = 10;
      minuteTen = 10;
      minuteOne = 10;
      break;

    case 2: // Flicker mode : all tubes
      if ((millis() / FLICKER_SPEED) % 2 == 0)
      {
        hourTen = 10;
        hourOne = 10;
        minuteTen = 10;
        minuteOne = 10;
      }
      break;

    case 3: // Flicker mode : hour tubes
      if ((millis() / FLICKER_SPEED) % 2 == 0)
      {
        hourTen = 10;
        hourOne = 10;
      }
      break;

    case 4: // Flicker mode : minute tubes
      if ((millis() / FLICKER_SPEED) % 2 == 0)
      {
        minuteTen = 10;
        minuteOne = 10;
      }
      break;

    case 5: // Flicker mode : hour ten tube
      if ((millis() / FLICKER_SPEED) % 2 == 0)
      {
        hourTen = 10;
      }
      break;

    case 6: // Flicker mode : hour one tube
      if ((millis() / FLICKER_SPEED) % 2 == 0)
      {
        hourOne = 10;
      }
      break;

    case 7: // Flicker mode : minute ten tube
      if ((millis() / FLICKER_SPEED) % 2 == 0)
      {
        minuteTen = 10;
      }
      break;

    case 8: // Flicker mode : minute one tube
      if ((millis() / FLICKER_SPEED) % 2 == 0)
      {
        minuteOne = 10;
      }
      break;
  }
}

void checkButtons()
{
  if (digitalRead(LEFT_BUTTON) == LOW && !LB_WAIT_FOR_RELEASE) // If Left Button is being pressed and we are not waiting for a release
  {
    if (LB_PRESSED) // If Left Button was already pressed before
    {
      Serial.println("LB still pressed");
      if (millis() - LB_PRESSED_TIME >= BUTTON_LONG_PRESS) // If Left Button was already pressed for more than the Long Press time
      {
        Serial.println("LB long press");
        LB_LP = true; // Left Button Long Press
        LB_PRESSED = false;
        LB_WAIT_FOR_RELEASE = true;
      }
    }
    else // If Left Button was not already pressed
    {
      Serial.println("LB pressed");
      LB_PRESSED = true;
      LB_PRESSED_TIME = millis();
    }
  }
  else if (digitalRead(LEFT_BUTTON) == HIGH) // If Left Button is not being pressed
  {
    if (LB_PRESSED) // If Left Button was pressed before
    {
      Serial.println("LB short press");
      LB_SP = true; // Left Button Short Press
    }
    
    LB_WAIT_FOR_RELEASE = false;
    LB_PRESSED = false;
  }

  if (digitalRead(RIGHT_BUTTON) == LOW && !RB_WAIT_FOR_RELEASE) // If Right Button is being pressed and we are not waiting for a release
  {
    if (RB_PRESSED) // If Right Button was already pressed before
    {
      Serial.println("RB still pressed");
      if (millis() - RB_PRESSED_TIME >= BUTTON_LONG_PRESS) // If Right Button was already pressed for more than the Long Press time
      {
        Serial.println("RB long press");
        RB_LP = true; // Right Button Long Press
        RB_PRESSED = false;
        RB_WAIT_FOR_RELEASE = true;
      }
    }
    else // If Right Button was not already pressed
    {
      Serial.println("RB pressed");
      RB_PRESSED = true;
      RB_PRESSED_TIME = millis();
    }
  }
  else if (digitalRead(RIGHT_BUTTON) == HIGH) // If Right Button is not being pressed
  {
    if (RB_PRESSED) // If Right Button was pressed before
    {
      Serial.println("RB short press");
      RB_SP = true; // Right Button Short Press
    }
    
    RB_WAIT_FOR_RELEASE = false;
    RB_PRESSED = false;
  }
}

void reinitializeButtons()
{
  LB_SP = false;
  RB_SP = false;
  LB_LP = false;
  RB_LP = false;
}

void updateStateMachine()
{
  switch(STATE)
  {
    case 0: // Display time
      Serial.println("Display time");

      checkEcoMode();

      if (LB_SP || RB_SP)
      {
        if (SHUTTER_MODE == 1)
        {
          RULE_ECO_MODE_BYPASS = true; // Bypass the eco mode by short press on one of the button if we are on eco mode
        } 
      }

      if (LB_LP || RB_LP)
      {
        STATE = 1; // Go to next menu : Time set
        initializeTimeSetMode();
      }
      break;

    case 1: // Time set
      Serial.println("Time set");

      // Overriding the tubes' digit with the time the user is entering
      hourTen = changedHourTen;
      hourOne = changedHourOne;
      minuteTen = changedMinuteTen;
      minuteOne = changedMinuteOne;
      Serial.print("Time : ");
      Serial.print(changedHourTen * 10 + changedHourOne);
      Serial.print(" : ");
      Serial.println(changedMinuteTen * 10 + changedMinuteOne);


      SHUTTER_MODE = ACTUAL_DIGIT_SELECTED + 5; // A bit clunky but easier than a lot of if else...

      if (LB_SP)
      {
        switch(ACTUAL_DIGIT_SELECTED)
        {
          case 0:
            if (changedHourTen >= 2)
            {
              changedHourTen = 0;
            }
            else if (changedHourTen == 1)
            {
              changedHourTen = 2;
              changedHourOne = 0; // Set the hour one to 0 if hour ten is 2, because hour one can only have a value from 0 to 4 if hour ten is 2
            }
            else
            {
              changedHourTen += 1;
            }
            break;

          case 1:
            if (changedHourTen >= 2 && changedHourOne >= 3)
            {
              changedHourOne = 0;
            }
            else if (changedHourOne >= 9)
            {
              changedHourOne = 0;
            }
            else
            {
              changedHourOne += 1;
            }
            break;

          case 2:
            if (changedMinuteTen >= 5)
            {
              changedMinuteTen = 0;
            }
            else
            {
              changedMinuteTen += 1;
            }
            break;

          case 3:
            if (changedMinuteOne >= 9)
            {
              changedMinuteOne = 0;
            }
            else
            {
              changedMinuteOne += 1;
            }
            break;
        }
      }
      else if (RB_SP)
      {
        if (ACTUAL_DIGIT_SELECTED >= 3)
        {
          ACTUAL_DIGIT_SELECTED = 0;
        }
        else
        {
          ACTUAL_DIGIT_SELECTED += 1;
        }
      }
      else if (LB_LP)
      {
        updateClock();
        STATE = 2; // Go to next menu : Hour format selection
      }
      else if (RB_LP)
      {
        updateClock();
        STATE = 0; // Go to Display time
      }
      break;

    case 2: // Hour format selection
      Serial.println("Hour format selection");

      // Overriding the tubes' digit with "12 24"
      hourTen = 1;
      hourOne = 2;
      minuteTen = 2;
      minuteOne = 4;

      if (RULE_TWELVE_HOURS)
      {
        SHUTTER_MODE = 3; // If the current rule is 12 hours, blink the hours tubes
      }
      else
      {
        SHUTTER_MODE = 4; // If the current rule is 24 hours, blink the minutes tubes
      }

      // Button actions :
      if (LB_SP)
      {
        STATE = 3; // Go to next menu : Hide leading zero selection
      }
      else if (RB_SP)
      {
        RULE_TWELVE_HOURS = !RULE_TWELVE_HOURS; // Switch the rule
      }
      else if (LB_LP)
      {
        STATE = 3; // Go to next menu : Hide leading zero selection
      }
      else if (RB_LP)
      {
        STATE = 0; // Go to Display time
      }
      break;

    case 3: // Hide leading zero selection
      Serial.println("Hide leading zero selection");

      // Overriding the tubes' digit with "01 x1"
      hourTen = 0;
      hourOne = 1;
      minuteTen = 10;
      minuteOne = 1;

      if (!RULE_HIDE_LEADING_ZERO_HOURS)
      {
        SHUTTER_MODE = 3; // If the current rule choice is no hide, blink the hours tubes
      }
      else
      {
        SHUTTER_MODE = 4; // If the current rule choice hide, blink the minutes tubes
      }

      if (LB_SP)
      {
        STATE = 4; // Go to next menu : Eco mode selection
      }
      else if (RB_SP)
      {
        RULE_HIDE_LEADING_ZERO_HOURS = !RULE_HIDE_LEADING_ZERO_HOURS; // Switch the rule
      }
      else if (LB_LP)
      {
        STATE = 4; // Go to next menu : Eco mode selection
      }
      else if (RB_LP)
      {
        STATE = 0; // Go to Display time
      }
      break;

    case 4: // Eco mode selection
      Serial.println("Eco mode selection");

      // Overriding the tubes' digit with eco mode start and stop hours
      hourTen = RULE_ECO_MODE_START_TIME / 10;
      hourOne = RULE_ECO_MODE_START_TIME % 10;
      minuteTen = RULE_ECO_MODE_STOP_TIME / 10;
      minuteOne = RULE_ECO_MODE_STOP_TIME % 10;

      if (RULE_ECO_MODE_START_TIME_SELECTED)
      {
        SHUTTER_MODE = 3; // If the current rule choice is no hide, blink the hours tubes
      }
      else
      {
        SHUTTER_MODE = 4; // If the current rule choice hide, blink the minutes tubes
      }

      if (LB_SP)
      {
        if(RULE_ECO_MODE_START_TIME_SELECTED)
        {
          RULE_ECO_MODE_START_TIME += 1;
          if (RULE_ECO_MODE_START_TIME >= 24)
          {
            RULE_ECO_MODE_START_TIME = 0; // 24 hours rollback
          }
        }
        else
        {
          RULE_ECO_MODE_STOP_TIME += 1;
          if (RULE_ECO_MODE_STOP_TIME >= 24)
          {
            RULE_ECO_MODE_STOP_TIME = 0; // 24 hours rollback
          }
        }
      }
      else if (RB_SP)
      {
        RULE_ECO_MODE_START_TIME_SELECTED = !RULE_ECO_MODE_START_TIME_SELECTED; // Switch the time choice
      }
      else if (LB_LP)
      {
        STATE = 1; // Go to next menu : Time set
      }
      else if (RB_LP)
      {
        STATE = 0; // Go to Display time
      }
  }
}

// Update the RTC with the new set time
void updateClock()
{
  t.hour = changedHourTen * 10 + changedHourOne;
  t.min = changedMinuteTen * 10 + changedMinuteOne;
  DS3231_set(t);
}

// Update the RTC with a new year. The year on the RTC store the eco time slot in the following fashion : 
//    - eco start time as the RTC's day
//    - eco stop time as the tens of the RTC's year
void updateEcoTime()
{
  t.mday = RULE_ECO_MODE_START_TIME;
  t.year = 2000 + RULE_ECO_MODE_STOP_TIME; 
  DS3231_set(t);

  Serial.print("RTC day updated (eco mode start time) : ");
  Serial.println(t.mday);
  Serial.print("RTC year updated (eco mode stop time) : ");
  Serial.println(t.year);
}

// Set the clock in eco mode (tubes turned off) if actual time is in set eco time slot
void checkEcoMode()
{
  uint8_t actualHour = hourTen * 10 + hourOne;

  if (RULE_ECO_MODE_START_TIME > RULE_ECO_MODE_STOP_TIME && (actualHour >= RULE_ECO_MODE_START_TIME || actualHour < RULE_ECO_MODE_STOP_TIME))
  {
    if (RULE_ECO_MODE_BYPASS)
    {
      SHUTTER_MODE = 0; // Eco mode bypass -> Standard mode
    }
    else
    {
      SHUTTER_MODE = 1; // Eco mode
    }
  }
  else if (RULE_ECO_MODE_START_TIME < RULE_ECO_MODE_STOP_TIME && (actualHour >= RULE_ECO_MODE_START_TIME && actualHour < RULE_ECO_MODE_STOP_TIME))
  {
    if (RULE_ECO_MODE_BYPASS)
    {
      SHUTTER_MODE = 0; // Eco mode bypass -> Standard mode
    }
    else
    {
      SHUTTER_MODE = 1; // Eco mode
    }
  }
  else
  {
    SHUTTER_MODE = 0; // Standard mode
    RULE_ECO_MODE_BYPASS = false;
  }
}

void initializeTimeSetMode()
{
  changedHourTen = hourTen;
  changedHourOne = hourOne;
  changedMinuteTen = minuteTen;
  changedMinuteOne = minuteOne;
  ACTUAL_DIGIT_SELECTED = 0;
}
