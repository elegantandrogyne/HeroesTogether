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
const int WRITECYCLETIME = 4000;
const int PAGESIZE = 8; 

//BacPac commands:
const short int GET_BACPAC_PROTOCOL_VERSION = ('v' << 8) + 's';
const short int SET_BACPAC_3D_SYNC_READY    = ('S' << 8) + 'R';
const short int SET_BACPAC_SLAVE_SETTINGS   = ('X' << 8) + 'S';
const short int SET_BACPAC_HEARTBEAT        = ('H' << 8) + 'B';
const short int SET_BACPAC_POWER_DOWN       = ('P' << 8) + 'W';
const short int SET_BACPAC_SHUTTER_ACTION   = ('S' << 8) + 'H';

// Current camera mode is actually found in a string the camera sends: 
const short int SET_BACPAC_MODE             = ('C' << 8) + 'M';
const short int TD_MODE = 0x09;

// Pin definitions
const int LINE_1           = 3;  // Line 1 for power / mode switching
const int LINE_2           = 5;  // Line 2 for power / mode switching
const int RELEASE_SW       = 7;  // Software debounced; ON - video, OFF - photo
const int I2CINT           = 10; // (SS)
const int TRIG             = 11; // (MOSI)
const int BPRDY            = 12; // (MISO) Pulled up by camera
const int LED_OUT          = 13; // Arduino onboard LED; HIGH (= ON) while recording
const int HBUSRDY          = A0; // Herobus Ready line
const int PWRBTN           = A1; // Short to GND for power ON

// For reading the camera settings:
const int TD_BUFFER_SIZE = 0x29;
byte td[TD_BUFFER_SIZE];
const short int tdtable[] = {SET_BACPAC_MODE, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
boolean tdDone = false;

// Default camera mode: photo
static int cameraMode = 1;
static int newCameraMode = 1;

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

// Bacpac Commands:


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
    if (!tdDone) {
    pinMode(BPRDY, INPUT);
    delay(1000);
    pinMode(BPRDY, OUTPUT);
    digitalWrite(BPRDY, LOW);
    tdDone = true;
  }
    break;
  case SET_BACPAC_POWER_DOWN: // PW
  tdDone = false;
  return;
  default:
    break;
  }
}


void checkBacpacAndCameraCommands() {
  // check BacPac commands
  if (recvq) {
    if (!(recv[0] & 0x80)) {

      if (recv[0] == 0x25) {
        if (!tdDone) {
          queueIn("td");
        }
        } else if (recv[0] == 0x27) {
           memcpy((char *)td+1, recv, TD_BUFFER_SIZE-1);
           td[0] = TD_BUFFER_SIZE-1; td[1] = 'T'; td[2] = 'D';
        
            //a little debug function
            ledOn();
            delay(200);
            ledOff();
            delay(200);
            ledOn();
            delay(200);
            ledOff();
            delay(200);
            //end debug function
          cameraMode = SET_BACPAC_MODE;
          switch (cameraMode) {
            case 0:
              ledOn();
              delay(5000);
              ledOff();
              break;
            case 1:
              ledOn();
              delay(1000);
              ledOff();
              break;
            default:
              break;
          }
      }
    } else {
    bacpacCommand();
    }
    recvq = false;
  }
  
  // check camera commands
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
  isOn = true;
}

void powerOff() {
  queueIn("PW0");
  isOn = false;
}

void reboot() {
  static unsigned long time = 0;
  ledOn();
  // Set the current camera mode to turn on next...
  switch (cameraMode) {
    case 1:
      queueIn("DM1"); //photo
      break;
    case 2:
      queueIn("DM2"); //burst
      break;
    case 3:
      queueIn("DM0"); //video
      break;
    case 4:
      queueIn("DM4");
      break;
    case 5:
      queueIn("DM5");
      break;
    case 7:
      queueIn("DM7");
      break;
    default:
      break;
  }
  if (isOn) {
    time = millis();
    while (millis() < (time + 100)) {
      checkBacpacAndCameraCommands();
      }
    powerOff();
    time = millis();
    while (millis() < (time + 2000)) {
      checkBacpacAndCameraCommands();
      }
    powerOn();
  }
  ledOff();
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
int checkLine1() {
  static int lastButtonState = 0;
  static unsigned long lastDebounceTime = 0;
  
  int line1State = digitalRead(LINE_1);
  if (line1State != lastButtonState) {
    lastDebounceTime = millis();
  }
  if (millis() - lastDebounceTime > 100) {
  lastButtonState = line1State;
  return lastButtonState;
  }
}

int checkLine2() {
  static int lastButtonState = 0;
  static unsigned long lastDebounceTime = 0;
  
  int line2State = digitalRead(LINE_2);
  if (line2State != lastButtonState) {
    lastDebounceTime = millis();
  }
  if (millis() - lastDebounceTime > 100) {
  lastButtonState = line2State;
  return lastButtonState;
  }
}

void checkCameraMode(int line1State, int line2State){
  if (line1State == true && line2State == true){
    newCameraMode = 3;  //video
    reboot();
  } else if (line2State == true) {
    newCameraMode = 2;  // burst
    reboot();
  } else if (line1State == true) {
    newCameraMode = 1;  // photo
    reboot();
  } else {
    newCameraMode = 0;  // off
    if (isOn) {
    isOn = false;
    }
  }
  if (newCameraMode != cameraMode) {
    cameraMode = newCameraMode;
    reboot();
  }
}

boolean lastHerobusState = LOW;  // Will be HIGH when camera attached.

void setup() 
{
  setSlave();
  pinMode(LINE_1,     INPUT_PULLUP);
  pinMode(LINE_2,     INPUT_PULLUP);
  pinMode(RELEASE_SW, INPUT_PULLUP);
  pinMode(LED_OUT,    OUTPUT);
  pinMode(BPRDY,      OUTPUT); digitalWrite(BPRDY, LOW);    // Show camera MewPro attach. 
  pinMode(TRIG,       OUTPUT); digitalWrite(TRIG, LOW);
  pinMode(I2CINT,     INPUT);
  pinMode(HBUSRDY,    INPUT);
  pinMode(PWRBTN,     INPUT);
  checkCameraMode(checkLine1(), checkLine2()) ;     // depends on the mode switch
  ledOff();
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

  checkBacpacAndCameraCommands();
  checkCameraMode(checkLine1(), checkLine2());
  checkShutterRelease();
  turnLedOffInPhotoMode();
}

