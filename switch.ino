// Interface to simple mechanical switches with software debounce.
//
// There are possibly two types of switch functions in video mode, and we assign them in the sample code as:
//    SWITCH0_PIN --- ON = start recording --> OFF = do nothing --> ON = stop recording --> OFF = do nothing
//    SWITCH1_PIN --- ON = start recording --> OFF = stop recording
//    ONOFF_PIN   --- ON = power on        --> OFF = do nothing --> ON = power off      --> OFF = do nothing
//
// Simple mechanical switches require debounce. For software debounce, original code can be found at everywhere, for example,
//   http://www.arduino.cc/en/Tutorial/Debounce
//


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
      powerOn();
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
  int reading = (digitalRead(SWITCH0_PIN) ? 0 : (1 << 0)) | (digitalRead(SWITCH1_PIN) ? 0 : (1 << 1)) | (digitalRead(ONOFF_PIN) ? 0 : (1 << 1));
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

