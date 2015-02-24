/* 
   gopro-sync - a program for synchronized shutter release on multiple GoPro Hero 3+ Black edition cameras.

   Forked from orangkucing's MewPro: Control GoPro Hero 3+ Black from Arduino
   Simplified, customized and (hopefully) compatible with most micro-controllers...

   A piece of advice:
   After programming a microcontroller, connect to it via serial interface (need 3V3 UART? Raspberry Pi is excellent for that!)
   and issue a series of commands:
     @
     !
   That'll turn the camera on, set camera role to slave/master (we need slave; after issuing "!", the camera should dump parameters
   including battery status etc. - that means it's in slave role; the camera's shutter button should be inactive).
   This is very important, because during role change, a value is written to EEPROM, so you don't even have to program it yourself!
*/

// Include other C files:

// c_I2C: I2C interface (THIS PART CAN'T BE OPTED OUT)
#include <RLduino78.h>
#include <Wire.h> 
#include "bacpac-commands.ino"
#include "i2c.ino"
#include "queue.ino"


// e_Shutters: One or two remote shutters without contact bounce or chatter
#undef  USE_SHUTTERS
//#include "shutter.ino"

// f_Switches: One or two mechanical switches
#define  USE_SWITCHES
#include "switch.ino"

// All I/O parameters are in gopro-sync.h

#include <Arduino.h>      //change for other microcontrollers
#include "gopro-sync.h"

boolean lastHerobusState = LOW;  // Will be HIGH when camera attached.

void setup() 
{
  // Remark. Arduino Pro Mini 328 3.3V 8MHz is too slow to catch up with the highest 115200 baud.
  //     cf. http://forum.arduino.cc/index.php?topic=54623.0
  // Set 57600 baud or slower.
  Serial.begin(57600);
  
  setupShutter();
  setupSwitch();
  setupIRremote();
  setupLightSensor();
  setupPIRSensor();

  setupLED(); // onboard LED setup 
  pinMode(BPRDY, OUTPUT); digitalWrite(BPRDY, LOW);    // Show camera MewPro attach. 
  pinMode(TRIG, OUTPUT); digitalWrite(TRIG, LOW);

  // don't forget to switch pin configurations to INPUT.
  pinMode(I2CINT, INPUT);  // Teensy: default disabled
  pinMode(HBUSRDY, INPUT); // default: analog input
  pinMode(PWRBTN, INPUT);  // default: analog input
}

void loop() 
{
  // Attach or detach bacpac
  if (digitalRead(HBUSRDY) == HIGH) {
    if (lastHerobusState != HIGH) {
      pinMode(I2CINT, OUTPUT); digitalWrite(I2CINT, HIGH);
      lastHerobusState = HIGH;
      resetI2C();
    }
  } else {
    if (lastHerobusState != LOW) {
      pinMode(I2CINT, INPUT);
      lastHerobusState = LOW;
    }
  }

  checkTimeAlarms();
  checkBacpacCommands();
  checkCameraCommands();
  checkSwitch();
  checkIRremote();
  checkLightSensor();
  checkPIRSensor();
  checkVMD();
}

