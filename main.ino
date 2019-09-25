#include <FS.h>                   //this needs to be first, or it all crashes and burns...

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino

//needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager

#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include <ESP8266HTTPClient.h>    // for https://github.com/matt-williams/matrix-esp8266/blob/master/matrix-esp8266.ino

HTTPClient http;
String accessToken;
String lastMessageToken;

String createLoginBody(String user, String password) {
  String buffer;
  StaticJsonDocument<1000>  jsonBuffer;
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
  StaticJsonDocument<1000>  jsonBuffer;
  jsonBuffer["msgtype"] = "m.text";
  jsonBuffer["body"] = message;
  serializeJson(jsonBuffer, buffer);
  return buffer;
}

bool login(String user, String password) {
  bool success = false;

  String buffer;
  buffer = createLoginBody(user, password);

  String url = "http://corsanywhere.glitch.me/https://matrix.fuz.re/_matrix/client/r0/login";
  // Serial.printf("POST %s\n", url.c_str());

  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int rc = http.POST(buffer);
  Serial.printf("buffer %s\n", buffer.c_str());
  if (rc > 0) {
    Serial.printf("Login return code %d\n", rc);
    if (rc == HTTP_CODE_OK) {
      String body = http.getString();
      StaticJsonDocument<1000>  jsonBuffer;
      deserializeJson(jsonBuffer, body);
      String myAccessToken = jsonBuffer["access_token"];
      accessToken = String(myAccessToken.c_str());
      Serial.println(accessToken);
      success = true;
    }
  } else {
    Serial.printf("Error: %s\n", http.errorToString(rc).c_str());
  }

  return success;
}

bool sendMessage(String roomId, String message) {
  bool success = false;

  String buffer;
  buffer = createMessageBody(message);

  String url = "http://corsanywhere.glitch.me/https://matrix.fuz.re/_matrix/client/r0/rooms/" + roomId + "/send/m.room.message/" + String(millis()) + "?access_token=" + accessToken + "&limit=1";
  Serial.printf("PUT %s\n", url.c_str());

  http.begin(url);
  int rc = http.sendRequest("PUT", buffer);
  if (rc > 0) {
    //    Serial.printf("%d\n", rc);
    if (rc == HTTP_CODE_OK) {
      success = true;
    }
  } else {
    Serial.printf("Error: %s\n", http.errorToString(rc).c_str());
  }
  return success;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//for LED status
#include <Ticker.h>
Ticker ticker;

const byte LED_PIN = 2; // 13 for Sonoff S20, 2 for NodeMCU/ESP12 internal LED
const byte BUTTON_PIN = 0;

void tick()
{
  //toggle state
  int state = digitalRead(LED_PIN);  // get the current state of LED_PIN pin
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
char matrixUsername[50];
char matrixPassword[50];
char matrixRoom[200] = "!ppCFWxNWJeGbyoNZVw:matrix.fuz.re"; // #entropy:matrix.fuz.re
char matrixMessage[500] = "Test from esp8266";

//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println(F("Should save config"));
  shouldSaveConfig = true;
}


WiFiManager wifiManager;
void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println();

  //set led pin as output
  pinMode(LED_PIN, OUTPUT);
  // start ticker with 0.5 because we start in AP mode and try to connect
  ticker.attach(0.6, tick);

  pinMode(BUTTON_PIN, INPUT);

  //clean FS, for testing
  //SPIFFS.format();
  //show SPIFFS info for debug
  //FSInfo fs_info;
  //SPIFFS.info(fs_info);
  //Serial.printf("SPIFFS: totalBytes: %u\n", fs_info.totalBytes);
  //Serial.printf("SPIFFS: usedBytes : %u\n", fs_info.usedBytes);
  //Serial.printf("SPIFFS: blockSize  : %u\n", fs_info.blockSize);
  //Serial.printf("SPIFFS: pageSize      : %u\n", fs_info.pageSize);
  //Serial.printf("SPIFFS: maxOpenFiles: %u\n", fs_info.maxOpenFiles);
  //Serial.printf("SPIFFS: maxPathLength  : %u\n", fs_info.maxPathLength);


  //read configuration from FS json
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
        StaticJsonDocument<1000> jsonBuffer;
        auto error = deserializeJson(jsonBuffer, buf.get());
        if (error) {
          Serial.print(F("deserializeJson() failed with code "));
          Serial.println(error.c_str());
          return;
        } else {
          Serial.println(F("deserializeJson() successful:"));

          serializeJsonPretty(jsonBuffer, Serial);
          Serial.println();

          strcpy(matrixUsername, jsonBuffer["matrixUsername"]);
          strcpy(matrixPassword, jsonBuffer["matrixPassword"]);
          strcpy(matrixRoom, jsonBuffer["matrixRoom"]);
          strcpy(matrixMessage, jsonBuffer["matrixMessage"]);
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
  WiFiManagerParameter customMatrixUsername("Matrix username", "Matrix username", matrixUsername, 50);
  WiFiManagerParameter customMatrixPassword("Matrix password", "Matrix password", matrixPassword, 50);
  WiFiManagerParameter customMatrixRoom("Matrix room", "Matrix room", matrixRoom, 200);
  WiFiManagerParameter customMatrixMessage("Matrix message", "Matrix message", matrixMessage, 500);

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

  //set minimu quality of signal so it ignores AP's under that quality
  //defaults to 8%
  //wifiManager.setMinimumSignalQuality();

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
  strcpy(matrixUsername, customMatrixUsername.getValue());
  strcpy(matrixPassword, customMatrixPassword.getValue());
  strcpy(matrixRoom, customMatrixRoom.getValue());
  strcpy(matrixMessage, customMatrixMessage.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println(F("saving config"));
    StaticJsonDocument<1000> jsonBuffer;
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

  http.setReuse(true);
  if (login(matrixUsername, matrixPassword)) {
    Serial.println(F("Sucessfully athenticated"));
    sendMessage(matrixRoom, matrixMessage);
  }

}

bool button_state = 0;
void loop() {
  // put your main code here, to run repeatedly:
  button_state = digitalRead(BUTTON_PIN);
  if (button_state == 0) {
    wifiManager.resetSettings();
    delay(500);
    ESP.reset();
  }
}
