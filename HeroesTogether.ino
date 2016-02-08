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

// Pin definitions
const int LINE_1           = 3;  // Line 1 for power / mode switching
const int LINE_2           = 5;  // Line 2 for power / mode switching
const int RELEASE_SW       = 7;  // Shutter release button
const int I2CINT           = 10; // (SS)
const int TRIG             = 11; // (MOSI)
const int BPRDY            = 12; // (MISO) Pulled up by camera
const int LED_OUT          = 13; // Arduino onboard LED; HIGH (= ON) while recording
const int HBUSRDY          = A0; // Herobus Ready line
const int PWRBTN           = A1; // Short to GND for power ON

// Default camera mode: menu
static int cameraMode = 7;
static int lastCameraMode = 7;
static long cameraModeLastCheckTime = 0;
static int combinedMode = 0;
static int lastCombinedMode = 7;
static int lastLinesState = 7;
static long lastDebounceTime = 0;

// Powered on flag
static bool isOn = false;

// Recording flag
static bool isRecording = false;

// Busy flag
static bool cameraBusy = true;

// td sent
boolean tdDone = false;

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

void ledToggle()                         // if it was on - turn off, and vice versa
{
  digitalWrite(LED_OUT, !ledState);
  ledState = !ledState;
}

void blink(int times) {                  // blinking for e.g. signalizing the camera mode
  for (int i=0; i<(times * 2); i++) {          // or for other diagnostic output during development
    ledToggle();
    delay(100);
  }
}

void turnLedOffInPhotoMode() {                 // LED will light up on shutter release, but will not
  if ((cameraMode == 1) && (cameraBusy)) {     // turn off automatically - this problem is solved here
    delay(500);
    ledOff();
    cameraBusy = false;
  }
}

// Power control:

void powerOn()
{
  pinMode(PWRBTN, OUTPUT);                 // short the powerbutton (Herobus 12) to the ground
  digitalWrite(PWRBTN, LOW);
  delay(10);
  digitalWrite(PWRBTN, HIGH);
  pinMode(PWRBTN, INPUT);                  // release the powerbutton line
  isOn = true;                             // set the flag in the software
}

void powerOff() {
  static unsigned long time = millis();    // wait until camera processes any previous commands
    while (millis() < (time + 100)) {
      checkBacpacAndCameraCommands();
      }
  queueIn("PW0");                          // send a command to power the camera down
  time = millis();
  while (millis() < (time + 2000)) {       // wait again - poweroff takes quite a bit
    checkBacpacAndCameraCommands();
    }
  isOn = false;                            // set the flag in the software
}

void reboot() {
  ledOn();                                 // LED should light up for the process
  cameraBusy = true;                       // don't allow shutter release or mode change
  static unsigned long time = 0;
  if ((cameraMode != 7) && (isOn)) {       // change from one mode to another when on
    setCameraMode();
    powerOff();
    powerOn();
  } else if ((cameraMode != 7) && (!isOn)) {  // switching on - needs rebooting too!
    powerOn();                                // camera will start in a wrong mode...
    delay(5000);
    time = millis();
    while (millis() < (time + 100)) {
      resetHerobus();
      checkBacpacAndCameraCommands();
    }
    setCameraMode();                                 // set the new camera mode
    powerOff();                                      // reboot
    powerOn();
  }  else if ((cameraMode == 7) && (isOn)) {         // switching the camera off
    setCameraMode();
    powerOff();
  }
  cameraBusy = false;
  ledOff();                                 // after finishing turn the LED off
}

void setCameraMode() {                      // 0: video, 1: photo, 2: burst, 7: menu
  char cmd[3];
  static unsigned long time = 0;
  sprintf(cmd, "DM%d", cameraMode);
  blink(cameraMode);                        // indicate the new camera mode on switching
  queueIn(cmd);
  time = millis();
  while (millis() < (time + 100)) {
      checkBacpacAndCameraCommands();
  }
}

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
        case 3: d = 0x0b; break;
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

void checkBacpacAndCameraCommands() {
  // check BacPac commands
  if (recvq) {
  switch ((recv[1] << 8) + recv[2]) {
  case GET_BACPAC_PROTOCOL_VERSION:
    memcpy(buf, validationString, sizeof validationString);
    SendBufToCamera();
    queueIn("XS1");
    break;
  case SET_BACPAC_3D_SYNC_READY:
    switch (recv[3]) {
    case 0:
      digitalWrite(TRIG, HIGH);
      delay(1);
      digitalWrite(TRIG, LOW);
      break;
    case 1:                      // start recording or take photo
      ledOn();
      cameraBusy = true;
      break;
    default:                     // burst capture complete
      ledOff();
      cameraBusy = false;
      break;
    }
    break;
  case SET_BACPAC_SLAVE_SETTINGS:
    queueIn("XS0");
    break;
  case SET_BACPAC_HEARTBEAT:     // response to GET_CAMERA_SETTING
    if (!tdDone) {               // to exit 3D mode, emulate detach bacpac
    pinMode(BPRDY, INPUT);
    delay(1000);
    pinMode(BPRDY, OUTPUT);
    digitalWrite(BPRDY, LOW);
    tdDone = true;
  }
    break;
  case SET_BACPAC_POWER_DOWN:    // PW0
  tdDone = false;
  default:
    break;
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
  if (cameraMode == 0) {                  // flag recording status in video mode
  isRecording = true;
  }
}

// SET_CAMERA_3D_SYNCHRONIZE STOP_RECORD
void stopRecording()
{
  queueIn("SY0");
  isRecording = false;                     // set the flag to off and put the LED out
  cameraBusy = false;
  ledOff();
}

// Shutter release routine:

void checkShutterRelease()
{
  static unsigned long lastDebounceTime = 0;
  static int lastButtonState = 1;                   // we use negative logic here
  static int buttonState;

  // read switch with debounce
  int shState = (digitalRead(RELEASE_SW));
  if (shState != lastButtonState) {
    lastDebounceTime = millis();
  }
  if (millis() - lastDebounceTime > 5) {            // 5ms debounce time should be safe enough 
    if (shState != buttonState) {                   // in video mode, press button again
      if ((shState == 0) && (cameraMode == 0)       // to stop recording...
          && (isRecording)) {
        stopRecording();
      } else if (shState == 0) {                    // ...otherwise, take a photo / start recording:
        startRecording();
      }
      buttonState = shState;
    }
  }
  lastButtonState = shState;
}

int readLines() {                                       // checks the status on lines
  return (digitalRead(LINE_1) << 1) + digitalRead(LINE_2);
}

void setInitialMode() {
  int cameraMode = readLines();
  if (cameraMode == 3) {                                // set the "menu" mode for off
    cameraMode = 7;
  }
  reboot();
}

void checkLines() {
    int linesReading = readLines();
    if (linesReading != lastLinesState) {
      lastDebounceTime = millis();
    }
  if ((millis() - lastDebounceTime) > 50) {
    if (linesReading != combinedMode) {
      combinedMode = linesReading;
    }
  }
  lastCombinedMode = linesReading;
  lastLinesState = linesReading;
}

void checkCameraMode(){                                   // This uses a negative logic...
  if (combinedMode == 3) {
    combinedMode = 7;
  }
  if (combinedMode != lastCameraMode) {
    cameraModeLastCheckTime = millis();
  }
  if ((millis() - cameraModeLastCheckTime) > 1000) {      // allow some time to change
    if (cameraMode != combinedMode) {
      cameraMode = combinedMode;
      reboot();                                           // mode changed? then reboot
    }
  }
  lastCameraMode = combinedMode;
}

boolean lastHerobusState = LOW;  // Will be HIGH when camera attached.
void resetHerobus() {
  // Attach or detach bacpac
  if (digitalRead(HBUSRDY) == HIGH) {
    if (lastHerobusState != HIGH) {
      pinMode(I2CINT, OUTPUT);
      digitalWrite(I2CINT, HIGH);
      lastHerobusState = HIGH;
      resetI2C();
    }
  } else {
    if (lastHerobusState != LOW) {
      pinMode(I2CINT, INPUT);
      lastHerobusState = LOW;
    }
  }
}

void setup() {
  //setSlave();
  pinMode(LINE_1,     INPUT_PULLUP);
  pinMode(LINE_2,     INPUT_PULLUP);
  pinMode(RELEASE_SW, INPUT_PULLUP);
  pinMode(LED_OUT,    OUTPUT);
  pinMode(I2CINT,     INPUT);
  pinMode(HBUSRDY,    INPUT);
  pinMode(PWRBTN,     INPUT);
  pinMode(BPRDY,      OUTPUT); digitalWrite(BPRDY, LOW);    // show camera MewPro attach. 
  pinMode(TRIG,       OUTPUT); digitalWrite(TRIG, LOW);
  powerOn();                                                // initial powerup
  static long time = millis();                              // give it two seconds to turn on
  while ((millis() - time) < 2000) {                        // idle so that commands can be processed
  resetHerobus();                                           // and Herobus is active
  checkBacpacAndCameraCommands();
  }
  reboot();                                                 // check current state and restart or power down
}

void loop() {
  resetHerobus();
  checkLines();
  if ((!cameraBusy) || isRecording) {   // allow shutter release only if camera is not busy
  checkShutterRelease();                // or when it's recording (in video mode)
}
  if (!cameraBusy) {                    // lock camera mode change as long as it's busy
  checkCameraMode();
}
  checkBacpacAndCameraCommands();
  turnLedOffInPhotoMode();
}

