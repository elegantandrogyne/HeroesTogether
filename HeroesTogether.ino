#include <Wire.h>
#include <Arduino.h>
#define HT_BUFFER_LENGTH 64
#define I2C_NOSTOP false
#define I2C_STOP true
#define BUFFER_LENGTH     HT_BUFFER_LENGTH
#define TWI_BUFFER_LENGTH HT_BUFFER_LENGTH
#define WIRE              Wire

const int ID_SLAVE  = 5;
const int I2CEEPROM = 0x50;
const int SMARTY = 0x60;
const int WRITECYCLETIME = 5000;
const int PAGESIZE = 8; 

//BacPac commands:
const short int GET_BACPAC_PROTOCOL_VERSION = ('v' << 8) + 's';
const short int SET_BACPAC_3D_SYNC_READY    = ('S' << 8) + 'R';
const short int SET_BACPAC_SLAVE_SETTINGS   = ('X' << 8) + 'S';
const short int SET_BACPAC_HEARTBEAT        = ('H' << 8) + 'B';
const short int SET_BACPAC_MODE             = ('C' << 8) + 'M';
const short int SET_BACPAC_POWER_DOWN       = ('P' << 8) + 'W';
const short int SET_BACPAC_SHUTTER_ACTION   = ('S' << 8) + 'H';

// Pin definitions
const int POWER_SW         = 3;  // Power switch
const int RELEASE_SW       = 5;  // Software debounced; ON-start ON-stop
const int MODE_SW          = 7;  // Software debounced; ON - video, OFF - photo
const int I2CINT           = 10; // (SS)
const int TRIG             = 11; // (MOSI)
const int BPRDY            = 12; // (MISO) Pulled up by camera
const int LED_OUT          = 13; // Arduino onboard LED; HIGH (= ON) while recording
const int HBUSRDY          = A0; // Herobus Ready line
const int PWRBTN           = A1; // Short to GND for power ON

// Default camera mode: photo
static int cameraMode = 1;

// Queue section:

byte queue[HT_BUFFER_LENGTH];
volatile int queueb = 0, queuee = 0;

void emptyQueue()
{
  queueb = queuee = 0;
}

byte myRead()
{
  if (queueb != queuee) {
    byte c = queue[queueb];
    queueb = (queueb + 1) % HT_BUFFER_LENGTH;
    return c;
  }
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

// LED control:

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

// I2C commands:

byte buf[HT_BUFFER_LENGTH], recv[HT_BUFFER_LENGTH];
int bufp = 1;
volatile boolean recvq = false;

// interrupt
void receiveHandler(int numBytes)
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

void setSlave()
{
  byte d, id;
  WIRE.begin();
  WIRE.beginTransmission(I2CEEPROM);
  WIRE.write((byte) 0);
  WIRE.endTransmission(I2C_NOSTOP);
  WIRE.requestFrom(I2CEEPROM, 1);
  if (WIRE.available()) {
    id = WIRE.read();
  }
  resetI2C();
  if (id != ID_SLAVE) {
  
  // emulate detouching bacpac by releasing BPRDY line
  pinMode(BPRDY, INPUT);
  delay(1000);
  WIRE.begin();
  for (unsigned int a = 0; a < 16; a += PAGESIZE) {
    WIRE.beginTransmission(I2CEEPROM);
    WIRE.write((byte) a);
    for (int i = 0; i < PAGESIZE; i++) {
      switch ((a + i) % 4) {
        case 0: d = ID_SLAVE; break; // major (MOD1): 5 for slave
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
}

// Bacpac Commands:
boolean tDone = false;

// what does this mean? i have no idea...
unsigned char validationString[19] = { 18, 0, 0, 3, 1, 0, 1, 0x3f, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0 };

void bacpacCommand()
{
  switch ((recv[1] << 8) + recv[2]) {
  case GET_BACPAC_PROTOCOL_VERSION:
    ledOff();
    memcpy(buf, validationString, sizeof validationString);
    SendBufToCamera();
    queueIn("XS1");
    break;
  case SET_BACPAC_3D_SYNC_READY:
    switch (recv[3]) {
    case 0: // CAPTURE_STOP
      digitalWrite(TRIG, HIGH);
      delay(1);
      digitalWrite(TRIG, LOW);
      break;
    case 1: // CAPTURE_START
      break;
    case 2: // CAPTURE_INTERMEDIATE (PES only)
    case 3: // PES interim capture complete
      ledOff();
      break;
    default:
      break;
    }
    break;
  case SET_BACPAC_SLAVE_SETTINGS:
    queueIn("XS0");
    break;
  case SET_BACPAC_SHUTTER_ACTION: // SH
    buf[0] = 3; buf[1] = 'S'; buf[2] = 'Y'; buf[3] = recv[3];
    SendBufToCamera();
    return;    
    break;
  case SET_BACPAC_HEARTBEAT: // response to GET_CAMERA_SETTING
    // to exit 3D mode, emulate detach bacpac
    if (!tDone) {
    pinMode(BPRDY, INPUT);
    delay(1000);
    pinMode(BPRDY, OUTPUT);
    digitalWrite(BPRDY, LOW);
    tDone = true;
  }
    break;
  case SET_BACPAC_POWER_DOWN: // PW
  tDone = false;
  return;
  default:
    break;
  }
}


void checkCameraCommands()
{
  while (queueb != queuee)  {
    static boolean shiftable;
    byte c = myRead();
    switch (c) {
      case '\n':
        if (bufp != 1) {
          buf[0] = bufp - 1;
          bufp = 1;
          SendBufToCamera();
        }
        return;

      default:
        if (bufp >= 3 && isxdigit(c)) {
          c -= '0';
          if (c >= 10) {
            c = (c & 0x0f) + 9;
          }    
        }
        if (bufp < 4) {
          shiftable = true;
          buf[bufp++] = c;
        } else {
          if (shiftable) {
            buf[bufp-1] = (buf[bufp-1] << 4) + c;
          } else {
            buf[bufp++] = c;
          }
          shiftable = !shiftable;      
        }
        break;
    }
  }
    if (recvq) {
    bacpacCommand();
    recvq = false;
  }
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


static boolean isOn = false;


// Camera power On
void powerOn()
{
  pinMode(PWRBTN, OUTPUT);
  digitalWrite(PWRBTN, LOW);
  delay(1000);
  pinMode(PWRBTN, INPUT);
}

void powerOff() {
  queueIn("PW0");
}

void reboot() {
  static unsigned long time = 0;
  ledOn();
  // Set the current camera mode to turn on next...
  switch (cameraMode) {
    case 0:
      queueIn("DM0");
      break;
    case 1:
      queueIn("DM1");
      break;
    default:
      break;
  }
  checkPower();
  if (isOn) {
    time = millis();
    while (millis() < (time + 100)) {
      checkCameraCommands();
      }
    powerOff();
    time = millis();
    while (millis() < (time + 2000)) {
      checkCameraCommands();
      }
    powerOn();
  }
  ledOff();
}


// POWER_SW line: short to GND for ON and leave floating for OFF

void checkPower() {
  static int lastButtonState = 0;
  static int buttonState;
  static unsigned long lastDebounceTime = 0;
  
  int pwrState = digitalRead(POWER_SW);
  if (pwrState != lastButtonState) {
    lastDebounceTime = millis();
  }
  if (millis() - lastDebounceTime > 100) {
    if (pwrState != buttonState) {
      if ((pwrState == 0) && (!isOn)) {
      powerOn();
      isOn = true;
    } else if ((pwrState != 0) && (isOn)) {
      powerOff();
      isOn = false;
    }
    buttonState = pwrState;
    }
  }
  lastButtonState = pwrState;
}


// Shutter release routine:

void checkShutterRelease()
{
  static unsigned long lastDebounceTime = 0;
  static int lastButtonState = 0;
  static int buttonState;

  // read switch with debounce
  int shState = (digitalRead(RELEASE_SW));
  if (shState != lastButtonState) {
    lastDebounceTime = millis();
  }
  if (millis() - lastDebounceTime > 50) {
    if (shState != buttonState) {
      // In video mode, press button when the LED is on to stop recording...
      if ((shState == 0) && (cameraMode == 0) && (ledState == 1)) {
        stopRecording();
      // ...otherwise, take a photo / start recording:
      } else if (shState == 0) {
        startRecording();
      }
      buttonState = shState;
    }
  }
  lastButtonState = shState;
}

void turnLedOffInPhotoMode() {
  if ((cameraMode == 1) && (ledState)) {
    delay(500);
    ledOff();
  }
}


// default - photo; MODE_SW line: short to GND for video and leave floating for photo
void checkCameraMode() {
  static int lastButtonState = 0;
  static int buttonState;
  static unsigned long lastDebounceTime = 0;
  
  int cmState = digitalRead(MODE_SW);
  if (cmState != lastButtonState) {
    lastDebounceTime = millis();
  }
  if (millis() - lastDebounceTime > 100) {
    if (cmState != buttonState) {
      if ((cmState == 1) && (cameraMode == 0)) {           // set to photo mode
        cameraMode = 1;
        reboot();

      } else if ((cmState == 0) && (cameraMode == 1)) {    // set to video mode
        cameraMode = 0;
        reboot();
      }
    buttonState = cmState;
    }
  }
  lastButtonState = cmState;
}

boolean lastHerobusState = LOW;  // Will be HIGH when camera attached.

void setup() 
{
  pinMode(MODE_SW,    INPUT_PULLUP);
  pinMode(RELEASE_SW, INPUT_PULLUP);
  pinMode(POWER_SW,   INPUT_PULLUP);
  pinMode(LED_OUT,    OUTPUT);
  pinMode(BPRDY,      OUTPUT); digitalWrite(BPRDY, LOW);    // Show camera MewPro attach. 
  pinMode(TRIG,       OUTPUT); digitalWrite(TRIG, LOW);
  pinMode(I2CINT,     INPUT);
  pinMode(HBUSRDY,    INPUT);
  pinMode(PWRBTN,     INPUT);
  cameraMode = digitalRead(MODE_SW) ;     // depends on the mode switch
  ledOff();
  setSlave();
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

  checkCameraCommands();
  checkPower();
  checkCameraMode();
  checkShutterRelease();
  turnLedOffInPhotoMode();
}

