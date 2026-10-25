/*

  ET Phone Boat -- control a small RC sailboat from a phone with an ESP32

  how it works
  1. the ESP32 creates its own WiFi access point called ESP32_RCBOAT
  2. connect your phone to ESP32_RCBOAT, then open Safari and go to http://192.168.1.1
  3. the ESP sends the web page stored in BoatPage.h
  4. the web page uses AJAX (XMLHttpRequest) to send commands to the ESP (rudder slider, sail buttons)
  5. the web page also asks the ESP for JSON data every 200 ms to show the actual servo positions

  controls
  Rudder    -- slider, -45 (port) to +45 (starboard), servo on GPIO 18
  Main sail -- press and HOLD the IN or OUT button, the sail moves only while the button is held, servo on GPIO 19
  Jib       -- press and HOLD the IN or OUT button, the sail moves only while the button is held, servo on GPIO 21
  the sail servos can be continuous rotation (winch) or standard positional, see SAIL_CONTINUOUS

  safety
  - while a sail button is held, the page re-sends the command every 200 ms. If the ESP does not hear
    the command again within SAIL_HOLD_TIMEOUT ms (finger lifted and the STOP was lost, phone locked,
    out of range...) the sail stops where it is
  - if the ESP hears nothing at all from the phone for LINK_TIMEOUT ms, the rudder is centered

  required libraries
  ESP32_Servo  -- https://github.com/KrisKasprzak/ESP32_Servo
  ArduinoJson  -- https://github.com/bblanchon/ArduinoJson

  compile options
  1. esp32 dev module
  2. upload speed 921600
  3. cpu speed 240 mhz
  partition scheme default

  wiring
  servo signal wires to GPIO 18 (rudder), 19 (main sail), 21 (jib)
  servo V+ to a separate 5-6 V supply (NOT the ESP32 3.3 V pin), and connect that supply's GND
  to the ESP32 GND. A 470-1000 uF capacitor across the servo supply helps a lot.

  The MIT License (MIT)

*/

#include <WiFi.h>         // standard library
#include <WebServer.h>    // standard library
#include <ArduinoJson.h>  // https://github.com/bblanchon/ArduinoJson used to build the JSON data sent to the web page
#include <ESP32_Servo.h>  // servo library, see C:\__AI\MCULibraries\ESP32_Servo
#include "BoatPage.h"     // .h file that stores the html page code

// #define DEBUG

// access point the phone connects to
// the AP is open (no password) so you can just tap and connect
// to add a password, put it here (it must be at least 8 characters) -- leave as "" for an open network
#define AP_SSID "ESP32_RCBOAT"
#define AP_PASS ""

// servo pins
#define PIN_RUDDER 18
#define PIN_MAIN 19
#define PIN_JIB 21

// rudder settings
// the web page sends -RUDDER_MAX_DEFLECT to +RUDDER_MAX_DEFLECT, 0 is straight ahead
// servo angle = RUDDER_CENTER + value, flip RUDDER_REVERSED if the boat turns the wrong way
#define RUDDER_CENTER 90
#define RUDDER_MAX_DEFLECT 45
#define RUDDER_REVERSED false
#define RUDDER_SPEED 360  // deg/s, smooths out jumpy slider moves but still feels instant

// sail servo type
// 1 = continuous rotation (360 degree) servos, the usual choice for sheet winches (FS90R, SG90-360...)
//     the servo spins while IN or OUT is held and stops (neutral pulse) on release
// 0 = standard positional (180 degree) servos, the servo moves toward the IN or OUT angle while held
//     and holds its position on release
#define SAIL_CONTINUOUS 1

// continuous servo sail settings
#define SAIL_SPEED_PCT 60       // % of full speed while a button is held, lower = finer control
#define MAIN_REVERSED false     // flip if IN lets the main out
#define JIB_REVERSED false      // flip if IN lets the jib out
#define MAIN_NEUTRAL_US 1500    // adjust a few us if the main servo creeps when stopped
#define JIB_NEUTRAL_US 1500     // adjust a few us if the jib servo creeps when stopped
// a continuous servo has no position feedback, so the ESP estimates the sail position from how long
// it has been running. SAIL_TRAVEL_MS = time to go from fully in to fully out at SAIL_SPEED_PCT
// the estimate assumes the sails are half out at power up
#define SAIL_TRAVEL_MS 4000
// true = stop automatically when the estimate reaches fully in or fully out (protects the sheets)
// the estimate drifts over time, so leave false if it gets in the way
#define SAIL_SOFT_LIMITS false

// positional servo sail settings (SAIL_CONTINUOUS 0)
// servo angles for sheeted fully IN and fully OUT, swap the numbers if a sail moves the wrong way
#define MAIN_IN_ANGLE 30
#define MAIN_OUT_ANGLE 150
#define JIB_IN_ANGLE 30
#define JIB_OUT_ANGLE 150
#define SAIL_SPEED 45  // deg/s while a button is held, lower = finer control

// safety timeouts
#define SAIL_HOLD_TIMEOUT 600  // ms, stop a sail if the held button command is not repeated
#define LINK_TIMEOUT 3000      // ms, center the rudder if the phone goes silent

// sail motion state
enum SailDir { SAIL_STOP, SAIL_IN, SAIL_OUT };

struct Sail {
  const char* name;
  int pin;
  SailDir dir;
  uint32_t lastCommand;  // millis() of the last IN/OUT/STOP from the page
  long pressId;          // id of the button press now running
  long stoppedId;        // id of the last press that was stopped, late repeats of it are ignored
#if SAIL_CONTINUOUS
  ContinuousServo servo;
  bool reversed;
  float pos;             // estimated position, 0 = fully in, 100 = fully out
  uint32_t lastPosUpdate;
#else
  PositionalServo servo;
  float inAngle;
  float outAngle;
#endif
};

PositionalServo rudder;
Sail mainSail;
Sail jibSail;

int RudderValue = 0;       // last rudder value from the web page, -45 to 45
uint32_t LastContact = 0;  // millis() of the last request from the phone
bool LinkLost = false;

// the JSON text buffer, the JSON here is roughly 40 characters
char JSONTxt[128];

// definitions of the network created by the ESP32
IPAddress PageIP(192, 168, 1, 1);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

// gotta create a server
WebServer server(80);

void setup() {

  Serial.begin(115200);

  // attach the servos, default 500-2500 us pulse range for 0-180 degrees
  // if your servos buzz at the ends of travel, narrow the range e.g. attach(pin, 1000, 2000)
  if (!rudder.attach(PIN_RUDDER)) {
    Serial.println("Failed to attach rudder servo");
  }
  rudder.setSpeed(RUDDER_SPEED);
  rudder.setEasing(ServoEasing::Linear);
  rudder.setLimits(RUDDER_CENTER - RUDDER_MAX_DEFLECT, RUDDER_CENTER + RUDDER_MAX_DEFLECT);
  rudder.write(RUDDER_CENTER);

#if SAIL_CONTINUOUS
  SetupSail(mainSail, "Main", PIN_MAIN, MAIN_REVERSED, MAIN_NEUTRAL_US);
  SetupSail(jibSail, "Jib", PIN_JIB, JIB_REVERSED, JIB_NEUTRAL_US);
#else
  SetupSail(mainSail, "Main", PIN_MAIN, MAIN_IN_ANGLE, MAIN_OUT_ANGLE);
  SetupSail(jibSail, "Jib", PIN_JIB, JIB_IN_ANGLE, JIB_OUT_ANGLE);
#endif

  Serial.println("starting access point");

  // create the access point, an intranet with no internet connection
  // the phone connects to it and Safari goes to 192.168.1.1
  if (strlen(AP_PASS) >= 8) {
    WiFi.softAP(AP_SSID, AP_PASS);
  } else {
    WiFi.softAP(AP_SSID);
  }
  delay(100);
  WiFi.softAPConfig(PageIP, gateway, subnet);
  delay(100);

  Serial.print("Access point: ");
  Serial.println(AP_SSID);
  Serial.print("Open http://");
  Serial.println(WiFi.softAPIP());

  // page request, upon ESP getting / the web page will be sent
  server.on("/", SendWebsite);

  // the page asks for /json every 200 ms to show the servo positions
  server.on("/json", SendJSON);

  // commands from the web page
  // /RUDDER?VALUE=-12
  // /MAIN?DIR=IN   /MAIN?DIR=OUT   /MAIN?DIR=STOP
  // /JIB?DIR=IN    /JIB?DIR=OUT    /JIB?DIR=STOP
  server.on("/RUDDER", UpdateRudder);
  server.on("/MAIN", UpdateMain);
  server.on("/JIB", UpdateJib);

  // anything else (like iOS checking for internet) just gets the main page
  server.onNotFound(SendWebsite);

  server.begin();

  LastContact = millis();
}

void loop() {

  // no matter what you must call this handleClient repeatedly--otherwise the web page
  // will not get instructions to do something
  server.handleClient();

  // advance the servo motion (non-blocking)
  rudder.update();
  UpdateSail(mainSail);
  UpdateSail(jibSail);

  // phone went silent, center the rudder so the boat doesn't circle forever
  if (!LinkLost && (millis() - LastContact) > LINK_TIMEOUT) {
    LinkLost = true;
    RudderValue = 0;
    rudder.moveTo(RUDDER_CENTER);
    Serial.println("Link lost, centering rudder");
  }
}

#if SAIL_CONTINUOUS

void SetupSail(Sail& sail, const char* name, int pin, bool reversed, uint16_t neutralUs) {
  sail.name = name;
  sail.pin = pin;
  sail.dir = SAIL_STOP;
  sail.pressId = -1;
  sail.stoppedId = -1;
  sail.reversed = reversed;
  sail.pos = 50;
  sail.lastPosUpdate = millis();
  // attach() starts the servo at neutral, so it does not spin at power up
  if (!sail.servo.attach(pin)) {
    Serial.printf("Failed to attach %s servo on GPIO %d\n", name, pin);
  }
  sail.servo.setNeutral(neutralUs);
  sail.servo.stop();
}

// updates the estimated sail position from how long the winch has been running
void TrackSail(Sail& sail) {
  uint32_t now = millis();
  if (sail.dir != SAIL_STOP) {
    float delta = (now - sail.lastPosUpdate) * 100.0f / SAIL_TRAVEL_MS;
    sail.pos = constrain(sail.pos + (sail.dir == SAIL_OUT ? delta : -delta), 0.0f, 100.0f);
  }
  sail.lastPosUpdate = now;
}

// spin the winch IN or OUT, or stop it
void DriveSail(Sail& sail, SailDir dir) {
  // tracks the position estimate up to now before the speed changes
  TrackSail(sail);
  sail.dir = dir;
  if (dir == SAIL_STOP) {
    sail.servo.stop();
    return;
  }
  float speed = (dir == SAIL_OUT) ? SAIL_SPEED_PCT : -SAIL_SPEED_PCT;
  sail.servo.setSpeed(sail.reversed ? -speed : speed);
}

// keeps a sail moving while its button is held, and stops it if the commands stop coming
void UpdateSail(Sail& sail) {
  TrackSail(sail);
  if (sail.dir == SAIL_STOP) {
    return;
  }
  if ((millis() - sail.lastCommand) > SAIL_HOLD_TIMEOUT) {
    DriveSail(sail, SAIL_STOP);
    Serial.printf("%s hold timeout, stopping\n", sail.name);
  }
  else if (SAIL_SOFT_LIMITS && ((sail.dir == SAIL_IN && sail.pos <= 0) || (sail.dir == SAIL_OUT && sail.pos >= 100))) {
    DriveSail(sail, SAIL_STOP);
    Serial.printf("%s at its limit, stopping\n", sail.name);
  }
}

// sail position as 0 (fully in) to 100 (fully out) for the web page
int SailPercent(Sail& sail) {
  return (int)roundf(sail.pos);
}

#else

void SetupSail(Sail& sail, const char* name, int pin, float inAngle, float outAngle) {
  sail.name = name;
  sail.pin = pin;
  sail.dir = SAIL_STOP;
  sail.pressId = -1;
  sail.stoppedId = -1;
  sail.inAngle = inAngle;
  sail.outAngle = outAngle;
  if (!sail.servo.attach(pin)) {
    Serial.printf("Failed to attach %s servo on GPIO %d\n", name, pin);
  }
  sail.servo.setSpeed(SAIL_SPEED);
  // linear so a move that gets restarted still gives a steady motion
  sail.servo.setEasing(ServoEasing::Linear);
  // start half way out
  sail.servo.write((inAngle + outAngle) / 2.0f);
}

// move toward the IN or OUT angle, or hold where it is
void DriveSail(Sail& sail, SailDir dir) {
  sail.dir = dir;
  if (dir == SAIL_STOP) {
    sail.servo.stop();
    return;
  }
  sail.servo.moveTo(dir == SAIL_IN ? sail.inAngle : sail.outAngle);
}

// keeps a sail moving while its button is held, and stops it if the commands stop coming
void UpdateSail(Sail& sail) {
  if (sail.dir != SAIL_STOP && (millis() - sail.lastCommand) > SAIL_HOLD_TIMEOUT) {
    DriveSail(sail, SAIL_STOP);
    Serial.printf("%s hold timeout, stopping\n", sail.name);
  }
  sail.servo.update();
}

// sail position as 0 (fully in) to 100 (fully out) for the web page
int SailPercent(Sail& sail) {
  float pct = (sail.servo.read() - sail.inAngle) * 100.0f / (sail.outAngle - sail.inAngle);
  return constrain((int)roundf(pct), 0, 100);
}

#endif

// called by every request handler, keeps the link watchdog happy
void Contact() {
  LastContact = millis();
  if (LinkLost) {
    LinkLost = false;
    Serial.println("Link restored");
  }
}

// handles /RUDDER?VALUE=xx from the slider on the web page
void UpdateRudder() {

  Contact();

  // man I hate strings, but wifi lib uses them...
  RudderValue = constrain(server.arg("VALUE").toInt(), -RUDDER_MAX_DEFLECT, RUDDER_MAX_DEFLECT);

  int angle = RUDDER_REVERSED ? RUDDER_CENTER - RudderValue : RUDDER_CENTER + RudderValue;
  rudder.moveTo(angle);

#ifdef DEBUG
  Serial.printf("Rudder %d -> servo %d\n", RudderValue, angle);
#endif

  // YOU MUST SEND SOMETHING BACK TO THE WEB PAGE--BASICALLY TO KEEP IT LIVE
  server.send(200, "text/plain", "");
}

// shared processing for the main sail and jib buttons
// /MAIN?DIR=IN&ID=1234  -- every press of a button gets a new ID from the page, the held button
// repeats the same ID, and the STOP on release carries that ID too. Requests can arrive out of
// order, so a repeat that shows up after its STOP is ignored instead of restarting the sail
void ProcessSail(Sail& sail) {

  Contact();

  String dir = server.arg("DIR");
  long id = server.arg("ID").toInt();
  SailDir newDir = SAIL_STOP;
  if (dir == "IN") {
    newDir = SAIL_IN;
  } else if (dir == "OUT") {
    newDir = SAIL_OUT;
  }

  if (newDir == SAIL_STOP) {
    sail.stoppedId = id;
    // only stop if this STOP belongs to the press that is running (or nothing is running),
    // a late STOP from an earlier press must not cut off a new one
    if (id == sail.pressId || sail.dir == SAIL_STOP) {
      DriveSail(sail, SAIL_STOP);
    }
  } else if (id != sail.stoppedId) {
    sail.lastCommand = millis();
    // only change the motion on a new press or direction, the repeated commands from a held
    // button just refresh lastCommand so the hold timeout doesn't stop the sail
    if (id != sail.pressId || newDir != sail.dir) {
      sail.pressId = id;
      DriveSail(sail, newDir);
    }
  }

#ifdef DEBUG
  Serial.printf("%s %s %ld\n", sail.name, dir.c_str(), id);
#endif

  server.send(200, "text/plain", "");
}

void UpdateMain() {
  ProcessSail(mainSail);
}

void UpdateJib() {
  ProcessSail(jibSail);
}

// code to send the main web page
// PAGE_MAIN is a large char defined in BoatPage.h
void SendWebsite() {

  Contact();

  Serial.println("sending web page");

  server.send(200, "text/html", PAGE_MAIN);
}

// code to send the servo positions to the web page as JSON
// the web page calls this every 200 ms (see Process() in BoatPage.h)
// the JSON looks like this:
// {"R":-12,"M":45,"J":60}
// R = rudder value (-45 to 45), M = main sail % out, J = jib % out
void SendJSON() {

  Contact();

  JsonDocument doc;

  doc["R"] = RudderValue;
  doc["M"] = SailPercent(mainSail);
  doc["J"] = SailPercent(jibSail);

  if (measureJson(doc) >= sizeof(JSONTxt)) {
    Serial.println("JSON buffer is too small, increase the size of char JSONTxt[]");
  }

  serializeJson(doc, JSONTxt, sizeof(JSONTxt));

#ifdef DEBUG
  Serial.println(JSONTxt);
#endif

  server.send(200, "application/json", JSONTxt);
}

// end of code
