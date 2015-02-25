//   HeroesTogether - a program for synchronized power on/off and shutter release on multiple GoPro Hero 3+ Black edition cameras.
//
//   Forked from orangkucing's MewPro: Control GoPro Hero 3+ Black from Arduino
//   Simplified, customized and (hopefully) portable to most micro-controllers...
//

#include <Arduino.h>
#include <Wire.h> 
#define HT_BUFFER_LENGTH 64
#define I2C_NOSTOP false
#define I2C_STOP true
#define BUFFER_LENGTH     HT_BUFFER_LENGTH
#define TWI_BUFFER_LENGTH HT_BUFFER_LENGTH
#define WIRE              Wire

// GoPro Dual Hero EEPROM IDs
const int ID_MASTER = 4;
const int ID_SLAVE  = 5;

// I2C slave addresses
const int I2CEEPROM = 0x50;
const int SMARTY = 0x60;

// cycle time in milliseconds after block write
const int WRITECYCLETIME = 5000;


// page size for block write
const int PAGESIZE = 8; // 24XX01, 24XX02
// const int PAGESIZE = 16; // 24XX04, 24XX08, 24XX16

// Definitions of pins:

const int ONOFF_PIN        = 3;  // Software-debounced on-off button
const int SWITCH0_PIN      = 5;  // Software debounced; ON-start ON-stop
const int SWITCH1_PIN      = 6;  // Software debounced; ON-start OFF-stop
const int I2CINT           = 10; // (SS)
const int TRIG             = 11; // (MOSI)
const int BPRDY            = 12; // (MISO) Pulled up by camera
const int LED_OUT          = 13; // Arduino onboard LED; HIGH (= ON) while recording
const int HBUSRDY          = A0; // (14)
const int PWRBTN           = A1; // (15) Pulled up by camera

// Definitions of some BacPac command codes:

const int GET_BACPAC_PROTOCOL_VERSION = ('v' << 8) + 's';
const int SET_BACPAC_3D_SYNC_READY    = ('S' << 8) + 'R';
const int SET_BACPAC_POWER_DOWN       = ('P' << 8) + 'W';    //we'll need this...
const int SET_BACPAC_SLAVE_SETTINGS   = ('X' << 8) + 'S';
const int SET_BACPAC_HEARTBEAT        = ('H' << 8) + 'B';


byte queue[HT_BUFFER_LENGTH];
volatile int queueb = 0, queuee = 0;

void emptyQueue()
{
  queueb = queuee = 0;
}

// Utility functions
void queueIn(const char *p)
{
  int i;
  for (i = 0; p[i] != 0; i++) {
    queue[(queuee + i) % HT_BUFFER_LENGTH] = p[i];
  }
  queue[(queuee + i) % HT_BUFFER_LENGTH] = '\n';
  queuee = (queuee + i + 1) % HT_BUFFER_LENGTH;
}


byte buf[HT_BUFFER_LENGTH], recv[HT_BUFFER_LENGTH];
int bufp = 1;
volatile boolean recvq = false;

void receiveHandler(int numBytes)     // depends on platform

{
  int i = 0;
  while (WIRE.available()) {
    recv[i++] = WIRE.read();
    recvq = true;
  }
}

void requestHandler()
{
  if (strncmp((char *)buf, "\003SY", 3) == 0) {
    if (buf[3] == 1) {
      ledOn();
    } else {
      ledOff();
    }
  }
  WIRE.write(buf, (int) buf[0] + 1);
}


void SendBufToCamera() {
  digitalWrite(I2CINT, LOW);
  delayMicroseconds(30);
  digitalWrite(I2CINT, HIGH);
}

void resetI2C()
{
  WIRE.begin(SMARTY);
  WIRE.onReceive(receiveHandler);
  WIRE.onRequest(requestHandler);

  emptyQueue();
}

// Read I2C EEPROM to check if camera will be set up in master mode
boolean isSlave()
{
  byte id;
  WIRE.begin();
  WIRE.beginTransmission(I2CEEPROM);
  WIRE.write((byte) 0);
  WIRE.endTransmission(I2C_NOSTOP);
  WIRE.requestFrom(I2CEEPROM, 1);     // platform-dependent
  if (WIRE.available()) {
    id = WIRE.read();
  }

  resetI2C();
  return (id == ID_SLAVE);
}

// SET_CAMERA_3D_SYNCHRONIZE START_RECORD
void startRecording()
{
  queueIn("SY1");
}

// SET_CAMERA_3D_SYNCHRONIZE STOP_RECORD
void stopRecording()
{
  queueIn("SY0");
}

// When off, set this to 0
boolean poweredOn = false;

// Camera power On
void powerOn()
{
  pinMode(PWRBTN, OUTPUT);
  digitalWrite(PWRBTN, LOW);
  delay(1000);
  pinMode(PWRBTN, INPUT);
  poweredOn = true;
}

// Camera power Off
void powerOff()
{
  queueIn("PW0");
  poweredOn = false;
}

// Write I2C EEPROM
void roleChange()
{
  byte id, d;
  // emulate detouching bacpac by releasing BPRDY line
  pinMode(BPRDY, INPUT);
  delay(1000);

  id = isSlave() ? ID_MASTER : ID_SLAVE;
  
  WIRE.begin();
  for (unsigned int a = 0; a < 16; a += PAGESIZE) {
    WIRE.beginTransmission(I2CEEPROM);
    WIRE.write((byte) a);
    for (int i = 0; i < PAGESIZE; i++) {
      switch ((a + i) % 4) {
        case 0: d = id; break; // major (MOD1): 4 for master, 5 for slave
        case 1: d = 5; break;  // minor (MOD2) need to be greater than 4
        case 2: d = 0; break;
        case 3: d = 0; break;
      }
      WIRE.write(d);
    }
    WIRE.endTransmission(I2C_STOP);
    delayMicroseconds(WRITECYCLETIME);
  }
  pinMode(BPRDY, OUTPUT);
  digitalWrite(BPRDY, LOW);
  resetI2C();
}

boolean powerOnAtCameraMode = false;

void bacpacCommand()
{
  switch ((recv[1] << 8) + recv[2]) {
  case GET_BACPAC_PROTOCOL_VERSION:
    ledOff();
    buf[0] = 1; buf[1] = 1; // OK
    SendBufToCamera();
    delay(1000); // need some delay before I2C EEPROM read
    if (isSlave()) {
      queueIn("XS1");
    }

    break;
  case SET_BACPAC_3D_SYNC_READY:
    switch (recv[3]) {
    case 0: // CAPTURE_STOP
      // video stops at FALLING edge in MASTER NORMAL mode
      digitalWrite(TRIG, HIGH);
      delayMicroseconds(3);
      digitalWrite(TRIG, LOW);
      break;
    case 1: // CAPTURE_START
      if (powerOnAtCameraMode) {
        ledOff();
      }
      break;
    default:
      break;
    }
    break;
  case SET_BACPAC_SLAVE_SETTINGS:
    if ((recv[9] << 8) + recv[10] == 0) {
      powerOnAtCameraMode = true;
    }
  case SET_BACPAC_HEARTBEAT: // response to GET_CAMERA_SETTING
    // to exit 3D mode, emulate detach bacpac
    pinMode(BPRDY, INPUT);
    delay(1000);
    pinMode(BPRDY, OUTPUT);
    digitalWrite(BPRDY, LOW);
    break;
  default:
    break;
  }
}


void checkBacpacCommands()
{
  if (!recvq) {
    if (recv[0] & 0x80) {
      bacpacCommand();
    }
    recvq = false;
  }
}

boolean ledState;

void ledOff()
{
  digitalWrite(LED_OUT, LOW);
  ledState = false;
}

void ledOn()
{
  digitalWrite(LED_OUT, HIGH);
  ledState = true;
}

void setupLED()
{
  pinMode(LED_OUT, OUTPUT);
  ledOff();
}

//Switches control routine: SWITCH0 - start/stop recording, SWITCH1 - start recording (no stop), ONOFF - camera on/off

void switchClosedCommand(int state)
{

  switch (state) {
    case (1 << 0): // SWITCH0_PIN
      if (!ledState) {
        startRecording();
      } else {
        stopRecording();
      }
      break;
    case (1 << 1): // SWITCH1_PIN
      startRecording();
      break;
    case (1 << 2): // ONOFF_PIN
      if (!isSlave()) {
        roleChange();
      }
      if (!poweredOn) {
      powerOn();
      } else {
      powerOff();
      }
      break;
    default:
      break;
  }
}

void switchOpenedCommand(int state)
{
  switch (state) {
    case (1 << 0): // SWITCH0_PIN
      delay(100);
      break;
    case (1 << 1): // SWITCH1_PIN
      stopRecording();
      break;
    case (1 << 2): // ONOFF_PIN
      delay(1000);
      break;
    default:
      break;
  }
}

void setupSwitch()
{
  pinMode(SWITCH0_PIN, INPUT_PULLUP);
  pinMode(SWITCH1_PIN, INPUT_PULLUP);
  pinMode(ONOFF_PIN, INPUT_PULLUP);
}

void checkSwitch()
{
  static unsigned long lastDebounceTime = 0;
  static int lastButtonState = 0;
  static int buttonState;

  // read switch with debounce
  int reading = (digitalRead(SWITCH0_PIN) ? 0 : (1 << 0)) | (digitalRead(SWITCH1_PIN) ? 0 : (1 << 1)) | (digitalRead(ONOFF_PIN) ? 0 : (1 << 2));
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }
  if (millis() - lastDebounceTime > 100) {
    if (reading != buttonState) {
      if (reading != 0) {
        switchClosedCommand(reading);
      } else {
        switchOpenedCommand(buttonState);
      }
      buttonState = reading;
    }
  }
  lastButtonState = reading;
}


boolean lastHerobusState = LOW;  // Will be HIGH when camera attached.

void setup() 
{ 
  setupSwitch();

  setupLED(); // onboard LED setup 
  pinMode(BPRDY, OUTPUT); digitalWrite(BPRDY, LOW);    // Show camera HeroesTogether unit attach. 
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

  checkBacpacCommands();
  checkSwitch();
}

