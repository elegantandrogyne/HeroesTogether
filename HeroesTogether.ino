#include <Wire.h>
#include <Arduino.h>
#define MEWPRO_BUFFER_LENGTH 64
#define I2C_NOSTOP false
#define I2C_STOP true
#define BUFFER_LENGTH     MEWPRO_BUFFER_LENGTH
#define TWI_BUFFER_LENGTH MEWPRO_BUFFER_LENGTH
#define WIRE              Wire

// GoPro Dual Hero EEPROM IDs
const int ID_MASTER = 4;
const int ID_SLAVE  = 5;

// I2C slave addresses
const int I2CEEPROM = 0x50;
const int SMARTY = 0x60;
const int WRITECYCLETIME = 5000;
const int PAGESIZE = 8; 

//BacPac commands:
const int GET_BACPAC_PROTOCOL_VERSION = ('v' << 8) + 's';
const int SET_BACPAC_SHUTTER_ACTION   = ('S' << 8) + 'H';
const int SET_BACPAC_3D_SYNC_READY    = ('S' << 8) + 'R';
const int SET_BACPAC_WIFI             = ('W' << 8) + 'I'; // Defunct
const int SET_BACPAC_FAULT            = ('F' << 8) + 'N';
const int SET_BACPAC_POWER_DOWN       = ('P' << 8) + 'W';
const int SET_BACPAC_SLAVE_SETTINGS   = ('X' << 8) + 'S';
const int SET_BACPAC_HEARTBEAT        = ('H' << 8) + 'B';

// Pin definitions
const int POWER_SW         = 3;  // Power switch
const int SWITCH0_PIN      = 5;  // Software debounced; ON-start ON-stop
const int SWITCH1_PIN      = 6;  // Software debounced; ON-start OFF-stop
const int I2CINT           = 10; // (SS)
const int TRIG             = 11; // (MOSI)
const int BPRDY            = 12; // (MISO) Pulled up by camera
const int LED_OUT          = 13; // Arduino onboard LED; HIGH (= ON) while recording
const int HBUSRDY          = A0; // (14)
const int PWRBTN           = A1; // (15) Pulled up by camera

// Queue section:

byte queue[MEWPRO_BUFFER_LENGTH];
volatile int queueb = 0, queuee = 0;

void emptyQueue()
{
  queueb = queuee = 0;
}

boolean inputAvailable()
{
  if (queueb != queuee || Serial.available()) {
    return true;
  }
  return false;
}

byte myRead()
{
  if (queueb != queuee) {
    byte c = queue[queueb];
    queueb = (queueb + 1) % MEWPRO_BUFFER_LENGTH;
    return c;
  }
  return Serial.read();
}

// Utility functions
void queueIn(const char *p)
{
  int i;
  for (i = 0; p[i] != 0; i++) {
    queue[(queuee + i) % MEWPRO_BUFFER_LENGTH] = p[i];
  }
  queue[(queuee + i) % MEWPRO_BUFFER_LENGTH] = '\n';
  queuee = (queuee + i + 1) % MEWPRO_BUFFER_LENGTH;
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

byte buf[MEWPRO_BUFFER_LENGTH], recv[MEWPRO_BUFFER_LENGTH];
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

// print out debug information to Arduino serial console
void __printBuf(byte *p)
{
  int len = p[0] & 0x7f;

  for (int i = 0; i <= len; i++) {
    if (i == 1 && isprint(p[1]) || i == 2 && p[1] != 0 && isprint(p[2])) {
      if (i == 1) {
        Serial.print(' ');
      }
      Serial.print((char) p[i]);
    } else {
      char tmp[4];
      sprintf(tmp, " %02x", p[i]);
      Serial.print(tmp);
    }
  }
  Serial.println();
}

void _printInput()
{
  Serial.print('>');
  __printBuf(recv);
}

void SendBufToCamera() {
  Serial.print('<');
  __printBuf(buf);
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

// Read I2C EEPROM
boolean isMaster()
{
  byte id;
  WIRE.begin();
  WIRE.beginTransmission(I2CEEPROM);
  WIRE.write((byte) 0);
  WIRE.endTransmission(I2C_NOSTOP);
  WIRE.requestFrom(I2CEEPROM, 1);
  if (WIRE.available()) {
    id = WIRE.read();
  }

  resetI2C();
  return (id == ID_MASTER);
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

// Write I2C EEPROM
void roleChange()
{
  byte id, d;
  // emulate detouching bacpac by releasing BPRDY line
  pinMode(BPRDY, INPUT);
  delay(1000);

  id = isMaster() ? ID_SLAVE : ID_MASTER;
  
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

void checkCameraCommands()
{
  while (inputAvailable())  {
    static boolean shiftable;
    byte c = myRead();
    switch (c) {
      case ' ':
        continue;
      case '\n':
        if (bufp != 1) {
          buf[0] = bufp - 1;
          bufp = 1;
          SendBufToCamera();
        }
        return;
      case '@':
        bufp = 1;
        Serial.println(F("camera power on"));
        powerOn();
        while (inputAvailable()) {
          if (myRead() == '\n') {
            return;
          }
        }
        return;
      case '!':
        bufp = 1;
        Serial.println(F("role change"));
        roleChange();
        while (inputAvailable()) {
          if (myRead() == '\n') {
            return;
          }
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
          if (shiftable) { // TM requires six args; "TM0e080a0b2d03" sets time to 2014 Aug 10 11:45:03
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

// Bacpac Commands:

boolean powerOnAtCameraMode = false;

// what does this mean? i have no idea...
unsigned char validationString[19] = { 18, 0, 0, 3, 1, 0, 1, 0x3f, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0 };

void bacpacCommand()
{
  switch ((recv[1] << 8) + recv[2]) {
  case GET_BACPAC_PROTOCOL_VERSION:
    ledOff();
    memcpy(buf, validationString, sizeof validationString);
    SendBufToCamera();
    delay(1000); // need some delay before I2C EEPROM read
    if (isMaster()) {
      queueIn("VO1"); // SET_CAMERA_VIDEO_OUTPUT to herobus
    } else {
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
    // every second message will be off if we send "XS0" here
    queueIn("XS0");
    // battery level: 0-3 (4 if charging)
    Serial.print(F(" batt_level:")); Serial.print(recv[4]);
    // photos remaining
    Serial.print(F(" remaining:")); Serial.print((recv[5] << 8) + recv[6]);
    // photos on microSD card
    Serial.print(F(" photos:")); Serial.print((recv[7] << 8) + recv[8]);
    // video time remaining (sec)
    Serial.print(F(" seconds:")); Serial.print((recv[9] << 8) + recv[10]);
    // videos on microSD card
    Serial.print(F(" videos:")); Serial.print((recv[11] << 8) + recv[12]);
    {
      // maximum file size (4GB if FAT32, 0 means infinity if exFAT)
      // if one video file exceeds the limit then GoPro will divide it into smaller files automatically
      char tmp[13];
      sprintf(tmp, " %02xGB %02x%02x%02x", recv[13], recv[14], recv[15], recv[16]);
      Serial.print(tmp);
    }
    Serial.println();
    break;
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
  if (recvq) {
    _printInput();
    if (!(recv[0] & 0x80)) {// information bytes
      switch (recv[0]) {
        // Usual packet length (recv[0]) is 0 or 1.
        case 0x27: 
          break;
        default:
          // do nothing
          break;
      }
    } else { 
      bacpacCommand();
    }
    recvq = false;
  }
}

// Switches:

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
    default:
      break;
  }
}

void switchOpenedCommand(int state)
{
  switch (state) {
    case (1 << 0): // SWITCH0_PIN
      delay(1000);
      break;
    case (1 << 1): // SWITCH1_PIN
      stopRecording();
      break;
    default:
      break;
  }
}


void checkSwitch()
{
  static unsigned long lastDebounceTime = 0;
  static int lastButtonState = 0;
  static int buttonState;

  // read switch with debounce
  int reading = (digitalRead(SWITCH0_PIN) ? 0 : (1 << 0)) | (digitalRead(SWITCH1_PIN) ? 0 : (1 << 1));
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

static int isOn = 0;
void checkPower() {
  if ((digitalRead(POWER_SW) == 0) && (isOn == 0)) {
    powerOn();
    isOn = 1;
  } else if ((digitalRead(POWER_SW) != 0) && (isOn == 1)) {
    powerOff();
    isOn = 0;
  }
}


boolean lastHerobusState = LOW;  // Will be HIGH when camera attached.

void setup() 
{
  Serial.begin(57600);
  
  pinMode(SWITCH0_PIN, INPUT_PULLUP);
  pinMode(SWITCH1_PIN, INPUT_PULLUP);
  pinMode(POWER_SW, INPUT_PULLUP);
  pinMode(LED_OUT, OUTPUT);
  ledOff();
  pinMode(BPRDY, OUTPUT); digitalWrite(BPRDY, LOW);    // Show camera MewPro attach. 
  pinMode(TRIG, OUTPUT); digitalWrite(TRIG, LOW);

  // don't forget to switch pin configurations to INPUT.
  pinMode(I2CINT, INPUT);  // Teensy: default disabled
  pinMode(HBUSRDY, INPUT); // default: analog input
  pinMode(PWRBTN, INPUT);  // default: analog input
  if (isMaster()) {
    roleChange();
  }
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

  checkPower();
  checkBacpacCommands();
  checkCameraCommands();
  checkSwitch();
}

