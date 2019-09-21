#define RELAY_PIN 12
#define LED_PIN 13

void setup() {
  Serial.begin(115200);

  Serial.println("START");

  pinMode(LED_PIN, OUTPUT);

}

void loop() {

  digitalWrite(LED_PIN, HIGH);

  delay(1000);

  digitalWrite(LED_PIN, LOW);

  delay(1000);

  Serial.println("LED");

}
