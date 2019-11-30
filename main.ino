
#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
//needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#define ARDUINOJSON_DECODE_UNICODE 1
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <ESP8266mDNS.h>          // https://tttapa.github.io/ESP8266/Chap08%20-%20mDNS.html


#include "EspSaveCrash.h"         //DEBUG https://github.com/krzychb/EspSaveCrash
//EspSaveCrash SaveCrash;

//for LED status
#include <Ticker.h>
Ticker ticker;

const byte RELAY_PIN = 12;
const byte LED_PIN = 13; // 13 for Sonoff S20, 2 for NodeMCU/ESP12 internal LED
const byte BUTTON_PIN = 0;

bool fuzIsOpen = false;

void tick() {
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
  ticker.attach(0.2, tick);
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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include <ESP8266HTTPClient.h>    // for https://github.com/matt-williams/matrix-esp8266/blob/master/matrix-esp8266.ino

HTTPClient http;
String accessToken;
String lastMessageToken;

String createLoginBody(String user, String password) {
  String buffer;
  StaticJsonDocument<1000> jsonBuffer;
  //JsonObject& root = jsonBuffer.createObject();
  jsonBuffer["type"] = "m.login.password";
  jsonBuffer["user"] = user;
  jsonBuffer["password"] = password;
  jsonBuffer["identifier"]["type"] = "m.id.user";
  jsonBuffer["identifier"]["user"] = user;
  serializeJson(jsonBuffer, buffer);
  return buffer;
}

String createMessageBody(String message) {
  String buffer;
  StaticJsonDocument<1000> jsonBuffer;
  jsonBuffer["msgtype"] = "m.text";
  jsonBuffer["body"] = message;
  serializeJson(jsonBuffer, buffer);
  return buffer;
}

bool login(String user, String password) {
  bool success = false;

  String buffer;
  buffer = createLoginBody(user.substring(0, user.indexOf(":")), password);

  String url = "http://corsanywhere.glitch.me/https://" + user.substring(user.indexOf(":") + 1) + "/_matrix/client/r0/login";
  // Serial.printf("[login] POST %s\n", url.c_str());

  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int rc = http.POST(buffer);
  Serial.printf("buffer %s\n", buffer.c_str());
  if (rc > 0) {
    Serial.printf("[login] Login return code %d\n", rc);
    if (rc == HTTP_CODE_OK) {
      String body = http.getString();
      StaticJsonDocument<1000>  jsonBuffer;
      deserializeJson(jsonBuffer, body);
      String myAccessToken = jsonBuffer["access_token"];
      accessToken = String(myAccessToken.c_str());
      Serial.printf("[login] %s\n", accessToken.c_str());
      success = true;
    }
  } else {
    Serial.printf("[login] Error: %s\n", http.errorToString(rc).c_str());
  }

  return success;
}

bool sendMessage(String roomId, String message) {
  bool success = false;

  String buffer;
  buffer = createMessageBody(message);

  String url = "http://corsanywhere.glitch.me/https://" + roomId.substring(roomId.indexOf(":") + 1) + "/_matrix/client/r0/rooms/" + roomId + "/send/m.room.message/" + String(millis()) + "?access_token=" + accessToken + "&limit=1";
  Serial.printf("[sendMessage] PUT %s\n", url.c_str());

  http.begin(url);
  int rc = http.sendRequest("PUT", buffer);
  if (rc > 0) {
    Serial.printf("[sendMessage] %d\n", rc);
    if (rc == HTTP_CODE_OK) {
      success = true;
    }
  } else {
    Serial.printf("[sendMessage] Error: %s\n", http.errorToString(rc).c_str());
  }
  return success;
}

// sendMessageToMultiRooms calls sendMessage for each Matrix room separated by a space character in roomId
bool sendMessageToMultiRooms(String roomId, String message) {
  yield(); // just in case
  int pos = roomId.indexOf(" ");
  if (pos != -1) {
    if (!sendMessage(roomId.substring(0, pos), message)) {
      return false;
    }
    return sendMessageToMultiRooms(roomId.substring(pos + 1), message);
  }
  return sendMessage(roomId, message);
}

bool mentionedOnMatrix = false;
bool getMessages(String roomId) {
  bool success = false;

  String url = "http://corsanywhere.glitch.me/https://" + roomId.substring(roomId.indexOf(":") + 1) + "/_matrix/client/r0/rooms/" + roomId + "/messages?access_token=" + accessToken + "&limit=1";
  if (lastMessageToken == "") {
    url += "&dir=b";
  } else {
    url += "&dir=f&from=" + lastMessageToken;
  }
  Serial.printf("[getMessages] GET %s\n", url.c_str());

  http.begin(url);
  int rc = http.GET();
  if (rc > 0) {
    Serial.printf("[getMessages] %d\n", rc);
    if (rc == HTTP_CODE_OK) {
      String body = http.getString();
      StaticJsonDocument<1000> jsonBuffer;
      deserializeJson(jsonBuffer, body);
      if (lastMessageToken != "") {
        JsonArray chunks = jsonBuffer["chunk"];
        JsonObject chunk = chunks[0];
        String format = chunk["format"];
        JsonObject content = chunk["content"];
        if (content.containsKey("formatted_body")) {
          String formatted_body = content["formatted_body"];
          Serial.printf("[getMessages] Last message formatted_body: %s\n", formatted_body.c_str());
          if (formatted_body.indexOf("<a href=\"https://matrix.to/#/@" + matrixUsername + "\">" + matrixUsername.substring(0, matrixUsername.indexOf(":")) + "</a>") >= 0) {
            mentionedOnMatrix = true;
          }
        }
        if (content.containsKey("body")) {
          String body = content["body"];
          Serial.printf("[getMessages] Last message body: %s\n", body.c_str());
          if (body.indexOf(matrixUsername.substring(0, matrixUsername.indexOf(":")) + ":") == 0 || body.indexOf("@" + matrixUsername) >= 0) {
            mentionedOnMatrix = true;
          }
        }
        //read receipt
        if (chunk.containsKey("event_id")) {
          String event_id = chunk["event_id"];
          String receiptUrl = "http://corsanywhere.glitch.me/https://" + roomId.substring(roomId.indexOf(":") + 1) + "/_matrix/client/r0/rooms/" + roomId + "/receipt/m.read/" + event_id + "?access_token=" + accessToken;
          http.begin(receiptUrl);
          http.addHeader("Content-Type", "application/json");
          http.POST("");
          String receiptBody = http.getString();
          Serial.println("[getMessages] Receipt " + receiptBody);
        }
      }
      String myLastMessageToken = jsonBuffer["end"];
      lastMessageToken = String(myLastMessageToken.c_str());
      //Serial.println(lastMessageToken);
      success = true;
    }
  } else {
    Serial.printf("[getMessages] Error: %s\n", http.errorToString(rc).c_str());
  }

  return success;
}

// getMessageFromMultiRooms calls sendMessage for each Matrix room separated by a space character in roomId
bool getMessagesFromMultiRooms(String roomId) {
  yield(); // just in case
  int pos = roomId.indexOf(" ");
  if (pos != -1) {
    if (!getMessages(roomId.substring(0, pos))) {
      return false;
    }
    return getMessagesFromMultiRooms(roomId.substring(pos + 1));
  }
  return getMessages(roomId);
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
    StaticJsonDocument<800> jsonBuffer;
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

void morseSOSLED() { // ... ___ ...
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, LOW); // lit up
    delay(100);
    digitalWrite(LED_PIN, HIGH); // lit down
    delay(100);
  }
  delay(50);
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, LOW); // lit up
    delay(300);
    digitalWrite(LED_PIN, HIGH); // lit down
    delay(100);
  }
  delay(50);
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, LOW); // lit up
    delay(100);
    digitalWrite(LED_PIN, HIGH); // lit down
    delay(100);
  }
  delay(500);
}

HTTPClient http2;
// http2.setReuse(true);
void notifyFuzIsOpen() {
  yield(); // just in case
  http2.begin("http://presence-button.glitch.me/status?fuzisopen=" + String(fuzIsOpen));
  http2.setAuthorization(matrixUsername.c_str(), matrixPassword.c_str());
  int httpCode = http2.GET();
  Serial.println("[notifyFuzIsOpen] Ping GET status return code: " + String(httpCode));
  http2.end();
}

bool loggedInMatrix = false;
void setup() {
  Serial.begin(115200);
  Serial.println();

  SaveCrash.print(); // DEBUG

  //set relay pin as output
  pinMode(RELAY_PIN, OUTPUT);
  //set led pin as output
  pinMode(LED_PIN, OUTPUT);
  //set button pin as input
  pinMode(BUTTON_PIN, INPUT);

  // start ticker with 0.5 because we start in AP mode and try to connect
  ticker.attach(0.6, tick);

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
        StaticJsonDocument<800> jsonBuffer;
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
    StaticJsonDocument<800> jsonBuffer;
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

  http.setReuse(true);
  loggedInMatrix = login(matrixUsername, matrixPassword);
  if (loggedInMatrix) {
    Serial.println(F("Sucessfully athenticated"));
    //keep LED on
    digitalWrite(LED_PIN, LOW);
    //light up rotating light
    digitalWrite(RELAY_PIN, HIGH);
  }

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
    Serial.println(F("Button pressed (longpress handling)"));
    pressedTime = millis();
  }
  if (buttonState == LOW && previousButtonState == LOW && (millis() - pressedTime) > 5000) {
    wifiManager.resetSettings();
    SPIFFS.format();
    delay(500);
    ESP.reset();
  }

  if (!loggedInMatrix) { // send SOS in morse
    morseSOSLED();
    loggedInMatrix = login(matrixUsername, matrixPassword);
    return;
  }
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis > getMatrixMessagesInterval) {
    previousMillis = currentMillis;
    if (!getMessagesFromMultiRooms(matrixRoom)) {
      morseSOSLED();
      return;
    }
    if (mentionedOnMatrix) {
      digitalWrite(RELAY_PIN, HIGH);
      mentionedOnMatrix = false;
    }
    notifyFuzIsOpen();
  }

  bool relayState = digitalRead(RELAY_PIN);

  if (buttonState == LOW && previousButtonState == HIGH && relayState == HIGH && sendMessageToMultiRooms(matrixRoom, matrixMessage)) { // button just pressed while light is up, send message on Matrix
    delay(100);
    Serial.println("Button pressed");
    digitalWrite(RELAY_PIN, LOW);
    fuzIsOpen = true;
  }
  digitalWrite(LED_PIN, LOW); // light up the LED, in case we encounter temporary failure in getMessages()
  previousButtonState = buttonState;
}
