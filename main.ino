#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#define ARDUINOJSON_DECODE_UNICODE 1
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

ESP8266WiFiMulti wifiMulti;

const byte RELAY_PIN = 12;
const byte LED_PIN = 13; // 13 for Sonoff S20, 2 for NodeMCU/ESP12 internal LED
const byte BUTTON_PIN = 0;

const String matrixUsername = "presence:matrix.fuz.re";
const String matrixPassword = "XXX";
const String matrixRoom = "!XXX:XXX";
const String matrixMessage = "Test from Sonoff S20";

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

  String url = "http://corsanywhere.glitch.me/https://" + roomId.substring(roomId.indexOf(":") + 1) + "/_matrix/client/r0/rooms/" + roomId + "/send/m.room.message/" + String(millis()) + "?access_token=" + accessToken + "&limit=1";
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

bool mentionedOnMatrix = false;
bool getMessages(String roomId) {
  bool success = false;

  String url = "http://corsanywhere.glitch.me/https://" + roomId.substring(roomId.indexOf(":") + 1) + "/_matrix/client/r0/rooms/" + roomId + "/messages?access_token=" + accessToken + "&limit=1";
  if (lastMessageToken == "") {
    url += "&dir=b";
  } else {
    url += "&dir=f&from=" + lastMessageToken;
  }
  Serial.printf("GET %s\n", url.c_str());

  http.begin(url);
  int rc = http.GET();
  if (rc > 0) {
    Serial.printf("%d\n", rc);
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
          Serial.println(formatted_body);
          if (formatted_body.indexOf("<a href=\"https://matrix.to/#/@" + matrixUsername + "\">presence</a>") >= 0) {
            mentionedOnMatrix = true;
          }
        }
        if (content.containsKey("body")) {
          String body = content["body"];
          Serial.println(body);
          if (body.indexOf(matrixUsername.substring(0, matrixUsername.indexOf(":")) + ":") == 0) {
            mentionedOnMatrix = true;
          }
        }
      }
      String myLastMessageToken = jsonBuffer["end"];
      lastMessageToken = String(myLastMessageToken.c_str());
      //Serial.println(lastMessageToken);
      success = true;
    }
  } else {
    Serial.printf("Error: %s\n", http.errorToString(rc).c_str());
  }

  return success;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

  WiFi.mode(WIFI_STA);
  wifiMulti.addAP("XXX", "XXX");
  wifiMulti.addAP("XXX", "XXX");
  wifiMulti.addAP("XXX", "XXX");

  Serial.println("Connecting Wifi");
  while (wifiMulti.run() != WL_CONNECTED) { // Wait for the Wi-Fi to connect: scan for Wi-Fi networks, and connect to the strongest of the networks above
    delay(1000);
    Serial.print('.');
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  http.setReuse(true);
  loggedInMatrix = login(matrixUsername, matrixPassword);
  if (loggedInMatrix) {
    Serial.println("Sucessfully athenticated");
    //keep LED on
    digitalWrite(LED_PIN, LOW);
    //light up the light
    digitalWrite(RELAY_PIN, HIGH);
  } else {
    //switch LED off
    digitalWrite(LED_PIN, HIGH);
    //power down the light
    digitalWrite(RELAY_PIN, LOW);
  }
}

bool buttonState = HIGH;
bool previousButtonState = HIGH;
long previousMillis = 0;
const long getMatrixMessagesInterval = 5000;
void loop() {
  if (!loggedInMatrix) { // send SOS in morse
    morseSOSLED();
    return;
  }
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis > getMatrixMessagesInterval) {
    previousMillis = currentMillis;
    if (!getMessages(matrixRoom)) {
      morseSOSLED();
      return;
    }
    if (mentionedOnMatrix) {
      digitalWrite(RELAY_PIN, HIGH);
      mentionedOnMatrix = false;
    }
  }

  buttonState = digitalRead(BUTTON_PIN);
  bool relayState = digitalRead(RELAY_PIN);
  if (buttonState == LOW && previousButtonState == HIGH && relayState == HIGH && sendMessage(matrixRoom, matrixMessage)) { // button just pressed while light is up
    delay(100);
    Serial.println("Button pressed");
    digitalWrite(RELAY_PIN, LOW);
  }
  digitalWrite(LED_PIN, LOW); // light up the LED, in case we encounter temporary failure in getMessages()
  previousButtonState = buttonState;
}
