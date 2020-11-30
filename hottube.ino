// #define DEBUG // if DEBUG is defined, ethernet is disabled
#define SERIAL_ENABLED
#define LEDSTRIP
#include <SPI.h>
#include <Ethernet.h>
#include <OneWire.h>
#ifdef LEDSTRIP
#include <Adafruit_NeoPixel.h>
#endif

IPAddress ip(10,0,0,95);
static byte mac[] = { 0xDE,0xAD,0x69,0x2D,0x30,0x32 }; // DE:AD:69:2D:30:32
#define SERVER_PORT 80 // what port is our web server on

// Initialize the Ethernet server library
// with the IP address and port you want to use
// (port 80 is default for HTTP):
EthernetServer server(SERVER_PORT); // TODO https://forum.arduino.cc/index.php?topic=344538.0

#define DS18S20_Pin     2 //DS18S20 Signal pin on digital 2
#define FLOW_SENSOR     3 // (int1) interrupt-triggering flow sensor
#define BEEPER_PIN      4 // make soothing noises for the yumens
#define LEDSTRIP_PIN    5 // string of WS2812B LEDs for tub status display
#define LAMPSOCKET_PIN  6 // goes to the sideways electrical outlet
#define HEATER_PUMP_PIN 7 // to turn on heater circulator pump
#define JETS_PUMP_PIN   8 // to turn on jet blaster pump
#define METER_PIN       9 // analog meter connected to this pin
#define BLEACH_KNOB_PIN  A0 // how many cups of bleach were added?
#define HTR_THERM1_PIN   A1 // connected to lower heater element thermistor (white)
#define HTR_THERM2_PIN   A2 // connected to upper heater element thermistor (blue)
#define BLEACH_BTN_PIN   A3 // the button pressed to log bleach event
#define HTR_ELEMENT_PIN  A4 // sends out 5V to an SSR controlling heater element
#define JETS_REQUEST_PIN A5 // short this pin to ground to turn jets on or off

#include "DS18S20.h" // reads temperature from the one digital temp sensor

#define HTR_THERM_MINIMUM  200 // if thermistor reads lower than this, it's an overheat

#define PUMPMINTIME 5000 // minimum time to run heater pump
#define HTR_ELEMENT_DELAY 10000 // how long to delay HTR_ELEMENT_PIN after HEATER_PUMP_PIN
#define HYSTERESIS 0.5 // how many degrees lower then set_celsius before turning heater on
#define METER_TIME 1000 // how long to wait before updating meter in loop()
#define JETS_TIME_MAX 240 // maximum jets time in minutes
#define JETS_REQUEST_TIME 5 // minutes of jets requested
#define TEMP_VALID_MIN 10 // minimum celsius reading from temp sensor considered valid
#define TEMP_VALID_MAX 120 // maximum celsius reading from temp sensor considered valid
#define MAXREADINGAGE 60000 // maximum time since last valid reading to continue to use it
#define CLIENT_TIMEOUT 2000 // timeout on a slow client
#define BUFFER_SIZE 64 // 1024 was too big, it turns out, as was 512 after CORS
char buffer[BUFFER_SIZE];
int bidx = 0;

unsigned int htr_therm1, htr_therm2; // store average ADC value of two heaters
float set_celsius = 5; // 40.5555555C = 105F
float celsiusReading = 255; // stores valid value read from temp sensor
unsigned long updateMeter, jetsOffTime, lastTempReading = 0;
unsigned long time = 0;
#ifdef LEDSTRIP
Adafruit_NeoPixel LEDStrip = Adafruit_NeoPixel(29, LEDSTRIP_PIN, NEO_GRB + NEO_KHZ800);
#endif

volatile unsigned long flowCounter = 0; // stores the count from the reed switch sensor (odometer :)
volatile unsigned long flowLastTime; // stores the last time flowCount() was called
//volatile unsigned long lastFlowSpeed; // last time we calculated flowSpeed
unsigned int flowSpeed = 0; // this is where we store flowCounter after 1 second

#define MIN_FLOWSPEED 15 // number of flowCounter after one second of counting that means enough flow
#define FLOWSPEED_DEBOUNCE 50 // minimum time between events to avoid switch bounce
void flowCount() {
  unsigned long _debounce = millis() - flowLastTime; // store the time since last tick
  if (_debounce > FLOWSPEED_DEBOUNCE) {
    flowCounter++; // tick the odometer
    // flowSpeed = _debounce; // store the time since last tick
    flowLastTime = millis(); // update the timestamp
  }
}

#ifdef LEDSTRIP
void setLEDStrip(byte r, byte g, byte b) {
  for(byte i=0; i<LEDStrip.numPixels(); i++) {
    LEDStrip.setPixelColor(i, LEDStrip.Color(r,g,b));
    LEDStrip.show();
  }
}
#endif

void setMeter(float celsius) { // set analog temperature meter
  // PWM of 24 = 0 celsius
  //  114 = 20 C
  //  210 = 40 C
  //  from 20 to 40 degrees = 96 PWM so 4.8 PWM per degree
  //  PWM = (celsius * 4.8) + 18 works great above 2 celsius
  int pwm = constrain((int)(celsius * 4.8) + 18, 0, 255);
  analogWrite(METER_PIN,pwm);
}

void setup() {
  tone(BEEPER_PIN, 800, 1000); // make a beep (non-blocking function)
  pinMode(METER_PIN, OUTPUT); // enable the analog temperature meter
  pinMode(JETS_PUMP_PIN, OUTPUT);
  pinMode(HEATER_PUMP_PIN, OUTPUT);
  pinMode(HTR_ELEMENT_PIN, OUTPUT);
  pinMode(LAMPSOCKET_PIN, OUTPUT);
  analogWrite(METER_PIN, 20);  // move the needle to about -1 degree C
#ifdef SERIAL_ENABLED
  Serial.begin(57600);
  Serial.println("\n[backSoon]");
#endif
  digitalWrite(FLOW_SENSOR,HIGH); // enable internal pullup resistor on int1
  attachInterrupt(1, flowCount, CHANGE); // call flowCount() on pin change
#ifndef DEBUG
  Ethernet.begin(mac, ip);
  server.begin();
  //Serial.print("server is at ");
  //Serial.println(Ethernet.localIP());
#endif
  if (initTemp()) {
#ifdef SERIAL_ENABLED
    Serial.print("DS18B20 temp sensor found, degrees C = ");
    Serial.print(getTemp());
    Serial.print(" serial number = ");
    for (byte b=0; b<8; b++) Serial.print(DS18S20addr[b],HEX);
    Serial.println();
#endif
    setMeter(getTemp());
  }
#ifdef SERIAL_ENABLED
  else Serial.println("ERROR: DS18B20 temp sensor NOT found!!!");
#endif
#ifdef LEDSTRIP
  LEDStrip.begin(); // init LED strip
  setLEDStrip(0,255,0);
#endif
}

void redirectClient(EthernetClient* client) {
    client->println(F("HTTP/1.1 302"));
    client->println(F("Pragma: no-cache"));
    client->println(F("Access-Control-Allow-Origin: *"));
    client->println(F("Location: /"));
}

void sendResponse(EthernetClient* client) {
  if (strncmp("GET /sc/", (char*)buffer, 8) == 0) {
    set_celsius = atof(buffer+8);
    redirectClient(client);
    return;
  }
  else if (strncmp("GET /sf/", (char*)buffer, 8) == 0) {
    set_celsius = farenheitToCelsius(atof(buffer+8));
    redirectClient(client);
    return;
  }
  else if (strncmp("GET /l/on", (char*)buffer, 9) == 0) {
    digitalWrite(LAMPSOCKET_PIN,HIGH);
    redirectClient(client);
    return;
  }
  else if (strncmp("GET /l/off", (char*)buffer, 10) == 0) {
    digitalWrite(LAMPSOCKET_PIN,LOW);
    redirectClient(client);
    return;
  }
  else if (strncmp("GET /j/off", (char*)buffer, 10) == 0) {
    digitalWrite(JETS_PUMP_PIN,LOW); // deactivate jets (even though jetsOffTime will cause that)
    jetsOffTime = time; // it's turnoff time
    redirectClient(client);
    return;
  }
  else if (strncmp("GET /j/on/", (char*)buffer, 10) == 0) {
    int jetMinutes = atoi(buffer+10); // activate jets for x minutes
    if ((jetMinutes > 0) && (jetMinutes <= JETS_TIME_MAX)) {
      digitalWrite(JETS_PUMP_PIN,HIGH); // turn on jets
      jetsOffTime = time + (jetMinutes * 60000); // set turn-off time in minutes from now
    }
    redirectClient(client);
    return;
  }

  client->println(F("HTTP/1.1 200 OK"));
  client->println(F("Pragma: no-cache"));
  client->println(F("Access-Control-Allow-Origin: *"));
  float celsius = getTemp(); // query the DS18B20 temp sensor

  if (strncmp("GET /help", (char*)buffer, 9) == 0) {
    client->println(F("Content-Type: text/plain\n"));
    client->println(F("GET /sc/{DEGREES}"));
    client->println(F("  Set the temperature in degrees celsius.\n"));
 
    client->println(F("GET /sf/{DEGREES}"));
    client->println(F("  Set the temperature in degrees fahrenheit.\n"));
 
    client->println(F("GET /j/on/{MINUTES}"));
    client->println(F("GET /j/off"));
    client->println(F("  Turn the jets on for MINUTES or off.\n"));
 
    client->println(F("GET /sensors[.json]"));
    client->println(F("  All the sensor data [as json]\n"));
  }
  else if (strncmp("GET /sensors", (char*)buffer, 12) == 0) {
    if (strncmp("GET /sensors.json", (char*)buffer, 17) == 0) {
      client->println(F("Content-Type: application/json\n"));
    } else {
      client->println(F("Content-Type: text/plain\n"));
    }
    client->println(F("{"));

    client->print(F("  \"heater_pump\": "));
    client->println(digitalRead(HEATER_PUMP_PIN) ? "true," : "false,");

    client->print(F("  \"heater_element\": "));
    client->println(digitalRead(HTR_ELEMENT_PIN) ? "true," : "false,");
    client->print(F("  \"htr_therm1\": "));
    client->print(htr_therm1);
    client->print(F(",\n  \"htr_therm2\": "));
    client->print(htr_therm2);

    client->println(F(",\n  \"temperature\": {"));
    client->print(F("    \"celsius\": "));
    client->print(celsius);
    client->println(F(","));
    client->print(F("    \"fahrenheit\": "));
    client->print(celsiusToFarenheit(celsius));
    client->println(F("\n  },"));

    client->print(F("  \"DS18B20_sn0\": \"0x"));
    for (byte b=0; b<8; b++) client->print(DS18S20addr[b],HEX);
    client->print(F("\",\n"));
    
    client->print(F("  \"set_temp\": {\n"));
    client->print(F("    \"celsius\": "));
    client->print(set_celsius);
    client->print(F(",\n"));
    client->print(F("    \"fahrenheit\": "));
    client->print(celsiusToFarenheit(set_celsius));
    client->print(F("\n  },\n"));

    client->print(F("  \"jets\": "));
    client->print(jetsOffTime > time ? jetsOffTime - time : 0);
    client->print(F(",\n"));

    client->print(F("  \"flowSpeed\": "));
    client->print(flowSpeed);
    client->print(F(",\n"));

    client->print(F("  \"lampsocket_pin\": "));
    client->print(digitalRead(LAMPSOCKET_PIN) ? "true,\n" : "false,\n");

    client->print(F("  \"knob_ADC_value\": "));
    client->print(analogRead(BLEACH_KNOB_PIN));

    client->print(F(",\n  \"button_state\": "));
    client->print(digitalRead(BLEACH_BTN_PIN) ? "\"pressed\"\n" : "\"released\"\n");
    client->print(F("}\n"));
  }
  else {
    client->println(F("Content-Type: text/html\n"));
    // print the current readings, in HTML format:
    if (digitalRead(HEATER_PUMP_PIN)) {
      client->print(F("Heater pump is on! flowSpeed: "));
      client->println(flowSpeed);
      client->println();
    }
    if (digitalRead(HTR_ELEMENT_PIN)) {
      client->println(F("Heater element is on!"));
      client->println();
    }
    client->print(F("Temperature: "));
    client->print(celsius);
    client->print(F(" degrees C or "));
    client->print(celsiusToFarenheit(celsius));
    client->println(F(" degrees F<br />"));
    client->print(F("Set point: "));
    client->print(set_celsius);
    client->print(F(" degrees C or "));
    client->print(celsiusToFarenheit(set_celsius));
    client->println(F(" degrees F<br />"));
 
    client->println(F("<br />See <a href=\"/help.txt\">help.txt</a> for API information<br />"));
  }
}

void listenForEthernetClients() {
  // listen for incoming clients
  EthernetClient client = server.available();
  if (client) {
    Serial.print(client.remoteIP());
    boolean currentLineIsBlank = true; // an http request ends with a blank line
    unsigned long connectStart = millis();
    while (client.connected() && (millis() - connectStart < CLIENT_TIMEOUT) ) {
      if (client.available()) {
        char c = client.read();
        buffer[bidx++] = c;
        if (bidx == BUFFER_SIZE) bidx = BUFFER_SIZE - 1; // don't overflow the buffer!!!

        // if you've gotten to the end of the line (received a newline
        // character) and the line is blank, the http request has ended,
        // so you can send a reply
        if (c == '\n' && currentLineIsBlank) {
          sendResponse(&client);
          bidx = 0; // reset the buffer
          break;
        }
        if (c == '\n') {
          // you're starting a new line
          currentLineIsBlank = true;
        }
        else if (c != '\r') {
          // you've gotten a character on the current line
          currentLineIsBlank = false;
        }
      }
    }
    // give the web browser time to receive the data
    delay(1);
    Serial.println(" has finished");
    // close the connection:
    client.stop();
  }
}

unsigned long jetRequestDebounce = 0; // last time jets request pin was high
#define JETS_REQUEST_DEBOUNCE_TIME 900 // time in milliseconds to debounce pin
#define JETS_REQUEST_CANCEL_TIME 1500 // time in milliseconds to cancel jets altogether

// this routine is seriously influenced by the main loop waiting 850ms for each
// call to read the DS18S20 temperature sensor.  This will change when DS18S20.h
// is modified to non-blocking behavior.

void updateJets() {
  if (time > jetsOffTime) digitalWrite(JETS_PUMP_PIN,LOW); // it's turn-off time!
  if (digitalRead(JETS_REQUEST_PIN)) { // jets request button is not activated
    jetRequestDebounce = time; // record last time we were high (unactivated)
  } else if (time - jetRequestDebounce > JETS_REQUEST_DEBOUNCE_TIME) { // activated and it's not a bounce
    if (time - jetRequestDebounce > JETS_REQUEST_CANCEL_TIME) { // holding button down cancels jets
      jetsOffTime = time; // cancel jets
      digitalWrite(JETS_PUMP_PIN,LOW); // turn OFF the jets
    } else { // just trying to increment jet time
      if (digitalRead(JETS_PUMP_PIN)) { // if pump is already on
        jetsOffTime += JETS_REQUEST_TIME * 60000; // add the time increment
        if (jetsOffTime - time > (JETS_TIME_MAX * 60000)) jetsOffTime = time + (JETS_TIME_MAX * 60000); // constrain
      } else {
        jetsOffTime = time + JETS_REQUEST_TIME * 60000; // set the time increment starting now
        digitalWrite(JETS_PUMP_PIN,HIGH); // turn on the jets
      }
    }
  }
}


void updateHeaterElementState() { // heater element turns on, after delay, if temperature is safe
  if (digitalRead(HEATER_PUMP_PIN) && (flowSpeed > MIN_FLOWSPEED)) {
    digitalWrite(HTR_ELEMENT_PIN,HIGH); // turn on heater element after delay
  } else {
    digitalWrite(HTR_ELEMENT_PIN,LOW); // turn off heater element
  }
}

void loop() {
  time = millis();
  if (time - updateMeter >= METER_TIME ) {
    flowSpeed = flowCounter;
    flowCounter = 0;  // reset the flow counter
    float lastCelsiusReading = celsiusReading;
    celsiusReading = getTemp();
    setMeter(celsiusReading); // set the temperature meter
    byte colorTemp = constrain(((celsiusReading-20)/20)*255, 0, 255); // gradient from 0 at 20C, 255 at 40C
    // the next line is where we cause water chemistry to affect the color of the LEDs
    byte waterChemistry = 0; // how nasty is the water chemistry, 0 = clean, 255 = nasty
#ifdef LEDSTRIP
    setLEDStrip(colorTemp,waterChemistry,255-colorTemp); // if clean chemistry: blue at or below 20C, red at or above 40C
#endif
    if ((celsiusReading > TEMP_VALID_MIN) && (celsiusReading < TEMP_VALID_MAX)) {
      lastTempReading = time; // temperature sensor reported a sane value
    } else if (time - lastTempReading < MAXREADINGAGE) { // if the last reading isn't too old
      celsiusReading = lastCelsiusReading; // just use the last valid value
    }
#ifdef SERIAL_ENABLED
    Serial.println(celsiusToFarenheit(celsiusReading));
#endif
    updateMeter = time;
    if (celsiusReading + HYSTERESIS < set_celsius) {  // only turn on heat if HYSTERESIS deg. C colder than target
      digitalWrite(HEATER_PUMP_PIN,HIGH); // turn on pump
    }
    if (celsiusReading > set_celsius) { // if we reach our goal, turn off heater
      digitalWrite(HEATER_PUMP_PIN,LOW); // turn off heater pump
      digitalWrite(HTR_ELEMENT_PIN,LOW); // turn off heater element
    }
  }
  updateHeaterElementState(); // updates htr_therm1 and htr_therm2 and HTR_ELEMENT_PIN
#ifndef DEBUG
  listenForEthernetClients();
  updateJets();
#endif
}

