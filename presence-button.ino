#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
//needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#define ARDUINOJSON_DECODE_UNICODE 1
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <ESP8266mDNS.h>          // https://tttapa.github.io/ESP8266/Chap08%20-%20mDNS.html

#include <WiFiClientSecure.h> 

//for LED status
#include <Ticker.h>
Ticker ticker;

const byte RELAY_PIN = D2; // SHOULD BE 12 for Sonoff S20
const byte LED_PIN = LED_BUILTIN; // 13 for Sonoff S20, 2 for NodeMCU/ESP12 internal LED
const byte BUTTON_PIN = 0;

bool fuzIsOpen = false;

void blinkLED() {
  //toggle state
  bool state = digitalRead(LED_PIN);  // get the current state of LED_PIN pin
  digitalWrite(LED_PIN, !state);     // set pin to the opposite state
}

//gets called when WiFiManager enters configuration mode (for LED status)
void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());
  //entered config mode, make led toggle faster
  ticker.attach(0.5, blinkLED);
}


//define your default values here, if there are different values in config.json, they are overwritten.
String matrixUsername;
String matrixPassword;
String matrixRoom = "!ppCFWxNWJeGbyoNZVw:matrix.fuz.re"; // #entropy:matrix.fuz.re
String matrixMessage = "Test";

//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println(F("Should save config"));
  shouldSaveConfig = true;
}


WiFiManager wifiManager;

#include <ESP8266HTTPClient.h>    // for https://github.com/matt-williams/matrix-esp8266/blob/master/matrix-esp8266.ino

ESP8266WebServer httpServer(80); // webserver on port 80 https://github.com/esp8266/Arduino/blob/14262af0d19a9a3b992d5aa310a684d47b6fb876/libraries/ESP8266WebServer/examples/AdvancedWebServer/AdvancedWebServer.ino
void handleRoot() {
  String html =
    String("<!DOCTYPE HTML><html>") +
    "<head><meta charset=utf-8><title>presence</title></head><body>" +
    "<a href='/admin'>Admin</a><br>" +
    "Rotating light:  " + String(digitalRead(RELAY_PIN) == HIGH) + "<br>" +
    "Fuz is open:  " + String(fuzIsOpen) + "<br>" +
    "</body></html>";
  httpServer.send(200, "text/html", html);
}
void handleAdmin() {
  if (!httpServer.authenticate(matrixUsername.c_str(), matrixPassword.c_str())) {
    return httpServer.requestAuthentication();
  }
  for (int i = 0; i < httpServer.args(); i++) {
    if (httpServer.argName(i) == "disablerotatinglight") {
      digitalWrite(RELAY_PIN, LOW);
      continue;
    }
    if (httpServer.argName(i) == "enablerotatinglight") {
      digitalWrite(RELAY_PIN, HIGH);
      continue;
    }
    if (httpServer.argName(i) == "setfuzisopen") {
      fuzIsOpen = true;
      continue;
    }
    if (httpServer.argName(i) == "resetesp") {
      httpServer.sendHeader("Location", httpServer.uri(), true);
      httpServer.send(302, "text/plain", "");
      delay(500);
      ESP.reset();
      return;
    }
  }

  if (httpServer.method() == HTTP_POST) {
    for (int i = 0; i < httpServer.args(); i++) {
      if (httpServer.argName(i) == "matrixUsername") {
        matrixUsername = httpServer.arg(i);
        continue;
      }
      if (httpServer.argName(i) == "matrixPassword") {
        matrixPassword = httpServer.arg(i);
        continue;
      }
      if (httpServer.argName(i) == "matrixRoom") {
        matrixRoom = httpServer.arg(i);
        continue;
      }
      if (httpServer.argName(i) == "matrixMessage") {
        matrixMessage = httpServer.arg(i);
        continue;
      }
    }
    Serial.println(F("saving config"));
    const size_t capacity = JSON_OBJECT_SIZE(4) + 440; // https://arduinojson.org/v6/assistant/
    DynamicJsonDocument jsonBuffer(capacity);
    jsonBuffer["matrixUsername"] = matrixUsername;
    jsonBuffer["matrixPassword"] = matrixPassword;
    jsonBuffer["matrixRoom"] = matrixRoom;
    jsonBuffer["matrixMessage"] = matrixMessage;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println(F("failed to open config file for writing"));
    }

    serializeJson(jsonBuffer, Serial);
    Serial.println();
    serializeJson(jsonBuffer, configFile);
    configFile.close();
  }
  if (httpServer.args() > 0 || httpServer.method() == HTTP_POST) { // trim GET parameters and prevent resubmiting same form on refresh
    httpServer.sendHeader("Location", httpServer.uri(), true);
    return httpServer.send(302, "text/plain", "");
  }

  String html =
    String("<!DOCTYPE HTML><html>") +
    "<head><meta charset=utf-8><title>presence admin</title></head><body>" +
    (digitalRead(RELAY_PIN) == HIGH ? "<a href='?disablerotatinglight'>Disable rotating light</a>" : "<a href='?enablerotatinglight'>Enable rotating light</a>") + "<br>" +
    (fuzIsOpen ? "" : "<a href='?setfuzisopen'>Set Fuz as open</a>") + "<br>" +
    "<a href='?resetesp'>Reboot ESP</a>" + "<br><br>" +
    "<form method='post'>" +
    "<div><label for='matrixUsername'>matrixUsername  </label><input name='matrixUsername' id='matrixUsername' value='" + matrixUsername + "'></div>" +
    "<div><label for='matrixPassword'>matrixPassword  </label><input name='matrixPassword' id='matrixPassword' value='" + matrixPassword + "'></div>" +
    "<div><label for='matrixRoom'>matrixRoom  </label><input name='matrixRoom' id='matrixRoom' value='" + matrixRoom + "'></div>" +
    "<div><label for='matrixMessage'>matrixMessage  </label><input name='matrixMessage' id='matrixMessage' value='" + matrixMessage + "'></div>" +
    "<div><button>Submit</button></div></form>"
    "</body></html>";
  httpServer.send(200, "text/html", html);
}
void handleNotFound() {
  httpServer.send(404, "text/plain", httpServer.uri() + " not found");
}

BearSSL::WiFiClientSecure secureClient;
HTTPClient http;
void notifyFuzIsOpen(bool fuzIsOpen) {
  http.begin(secureClient, "https://presence-button-staging.glitch.me/status?fuzisopen=" + String(fuzIsOpen));
  http.setAuthorization(matrixUsername.c_str(), matrixPassword.c_str());
  int httpCode = http.GET();
  Serial.println("notifyFuzIsOpen return code: " + String(httpCode));
  http.end();
  if(httpCode != 200){ // something is wrong, bad network or misconfigured credentials
    ticker.attach(0.1, blinkLED);
  } else {
    ticker.detach();
  }
}

bool loggedInMatrix = false;
void setup() {
  Serial.begin(115200);
  Serial.println();

  //set relay pin as output
  pinMode(RELAY_PIN, OUTPUT);
  //set led pin as output
  pinMode(LED_PIN, OUTPUT);
  //set button pin as input
  pinMode(BUTTON_PIN, INPUT);

  // start ticker with 1 because we start in AP mode and try to connect
  ticker.attach(1.1, blinkLED);

  Serial.println(F("mounting FS..."));

  if (SPIFFS.begin()) {
    Serial.println(F("mounted file system"));
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println(F("reading config file"));
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println(F("opened config file"));
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        const size_t capacity = JSON_OBJECT_SIZE(4) + 440; // https://arduinojson.org/v6/assistant/
        DynamicJsonDocument jsonBuffer(capacity);
        auto error = deserializeJson(jsonBuffer, buf.get());
        if (error) {
          Serial.print(F("deserializeJson() failed with code "));
          Serial.println(error.c_str());
          return;
        } else {
          Serial.println(F("deserializeJson() successful:"));

          serializeJsonPretty(jsonBuffer, Serial);
          Serial.println();

          String m0 = jsonBuffer["matrixUsername"];
          matrixUsername = m0;
          String m1 = jsonBuffer["matrixPassword"];
          matrixPassword = m1;
          String m2 = jsonBuffer["matrixRoom"];
          matrixRoom = m2;
          String m3 = jsonBuffer["matrixMessage"];
          matrixMessage = m3;
        }
        configFile.close();
      }
    }
  } else {
    Serial.println(F("failed to mount FS"));
  }
  //end read



  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter customMatrixUsername("Matrix username", "Matrix username", matrixUsername.c_str(), 50);
  WiFiManagerParameter customMatrixPassword("Matrix password", "Matrix password", matrixPassword.c_str(), 50);
  WiFiManagerParameter customMatrixRoom("Matrix room", "Matrix room", matrixRoom.c_str(), 200);
  WiFiManagerParameter customMatrixMessage("Matrix message", "Matrix message", matrixMessage.c_str(), 500);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  //WiFiManager wifiManager;
  //reset settings - for testing
  //wifiManager.resetSettings();

  //set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode (for status LED)
  wifiManager.setAPCallback(configModeCallback);

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //set static ip
  //  wifiManager.setSTAStaticIPConfig(IPAddress(10,0,1,99), IPAddress(10,0,1,1), IPAddress(255,255,255,0));

  //add all your parameters here
  wifiManager.addParameter(&customMatrixUsername);
  wifiManager.addParameter(&customMatrixPassword);
  wifiManager.addParameter(&customMatrixRoom);
  wifiManager.addParameter(&customMatrixMessage);

  //reset settings - for testing
  //wifiManager.resetSettings();

  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  //wifiManager.setTimeout(120);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect()) {
    Serial.println(F("failed to connect and hit timeout"));
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println(F("connected...yeey :)"));
  ticker.detach();
  //keep LED on
  digitalWrite(LED_PIN, LOW);

  //read updated parameters
  matrixUsername = customMatrixUsername.getValue();
  matrixPassword = customMatrixPassword.getValue();
  matrixRoom = customMatrixRoom.getValue();
  matrixMessage = customMatrixMessage.getValue();

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println(F("saving config"));
    const size_t capacity = JSON_OBJECT_SIZE(4) + 440; // https://arduinojson.org/v6/assistant/
    DynamicJsonDocument jsonBuffer(capacity);
    jsonBuffer["matrixUsername"] = matrixUsername;
    jsonBuffer["matrixPassword"] = matrixPassword;
    jsonBuffer["matrixRoom"] = matrixRoom;
    jsonBuffer["matrixMessage"] = matrixMessage;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println(F("failed to open config file for writing"));
    }

    serializeJson(jsonBuffer, Serial);
    Serial.println();
    serializeJson(jsonBuffer, configFile);
    configFile.close();
    //end save
  }

  Serial.println(F("local ip:"));
  Serial.println(WiFi.localIP());
  Serial.println(WiFi.SSID());

  httpServer.on("/", handleRoot);
  httpServer.on("/admin", handleAdmin);
  httpServer.onNotFound(handleNotFound);
  httpServer.begin();
  Serial.println("HTTP server started");
  if (!MDNS.begin("presence")) { // https://github.com/esp8266/Arduino/blob/14262af0d19a9a3b992d5aa310a684d47b6fb876/libraries/ESP8266mDNS/examples/mDNS_Web_Server/mDNS_Web_Server.ino
    Serial.println("Error setting up MDNS responder!");
  } else  {
    Serial.println("mDNS responder started");
  }
  MDNS.addService("http", "tcp", 80);

  secureClient.setInsecure();
}

bool buttonState = HIGH;
bool previousButtonState = HIGH;
long previousMillis = 0;
const long getMatrixMessagesInterval = 5000;
unsigned long pressedTime = millis();
void loop() {
  MDNS.update();

  httpServer.handleClient();

  buttonState = digitalRead(BUTTON_PIN);
  if (buttonState == LOW && previousButtonState == HIGH) { // long press handling, reset settings https://forum.arduino.cc/index.php?topic=276789.msg1947963#msg1947963
    Serial.println("Button pressed (longpress handling)");
    pressedTime = millis();
  }
  if (buttonState == LOW && previousButtonState == LOW && (millis() - pressedTime) > 5000) {
    Serial.println("Button STILL pressed (longpress handling)");
    wifiManager.resetSettings();
    SPIFFS.format();
    delay(500);
    ESP.reset();
  }

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis > getMatrixMessagesInterval) {
    previousMillis = currentMillis;
    notifyFuzIsOpen(fuzIsOpen);
  }

  if(!fuzIsOpen){
    digitalWrite(RELAY_PIN, HIGH);
  }

  bool relayState = digitalRead(RELAY_PIN);

  if (buttonState == LOW && previousButtonState == HIGH /* && relayState == HIGH*/) { // button just pressed while light is up
    delay(100);
    Serial.println("Button pressed");
    digitalWrite(RELAY_PIN, LOW);
    fuzIsOpen = true;
  }
  digitalWrite(LED_PIN, LOW); // ensure the LED is lit
  previousButtonState = buttonState;
}
