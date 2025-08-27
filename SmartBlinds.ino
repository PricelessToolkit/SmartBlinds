/* Smart Blind with 15s WiFi/MQTT timeouts + auto-sleep if no command arrives */

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <Servo.h>

#define STASSID "Change_This"
#define STAPSK  "Change_This"

uint8_t home_mac[6] = { 0xB2, 0xFF, 0xC8, 0xE2, 0xC2, 0xAA };
int channel = 6;

const char* ssid     = STASSID;
const char* password = STAPSK;

const char* mqtt_username = "Change_This";
const char* mqtt_password = "Change_This";
const char* mqtt_server   = "Change_This";
const int   mqtt_port     = 1883;

const char* TOPIC_BATTERY = "SmartBlinds/Blind-1/Battery";
const char* TOPIC_ANGLE   = "SmartBlinds/Blind-1/Angle";

WiFiClient espClient;
PubSubClient client(espClient);

// Static IP
IPAddress local_IP(192, 168, 2, 200);
IPAddress gateway (192, 168, 2, 1);
IPAddress subnet  (255, 255, 255, 0);

static const uint64_t TimeToSleepUs = 600ULL * 1000000ULL; // 600s = 10 min
const unsigned long WIFI_TIMEOUT_MS = 15000UL;
const unsigned long MQTT_TIMEOUT_MS = 15000UL;
const unsigned long MQTT_WAIT_CMD_MS = 5000UL; // wait max 5s for command after connect

Servo servo;
const int ServoPin       = 5;
const int ServoEnablePin = 4;

// RTC memory for last angle
struct RtcState {
  uint32_t magic;
  uint32_t angle;
  uint32_t checksum;
};
static const uint32_t RTC_MAGIC = 0xB1A9D501;
static const uint32_t RTC_SLOT  = 65;

static uint32_t rtcChecksum(const RtcState& s) {
  return (s.magic ^ s.angle) ^ 0xA5A5A5A5;
}

// globals for post-connect sleep
bool commandReceived = false;
unsigned long connectTime = 0;

void goToSleep() {
  digitalWrite(ServoEnablePin, LOW);
  servo.detach();

  if (client.connected()) client.disconnect();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(10);

  ESP.deepSleep(TimeToSleepUs, WAKE_RF_DEFAULT);
}

void setup() {
  Serial.begin(115200);

  pinMode(ServoEnablePin, OUTPUT);
  digitalWrite(ServoEnablePin, LOW);
  servo.attach(ServoPin);

  WiFi.persistent(false);

  WiFi.config(local_IP, gateway, subnet);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password, channel, home_mac, true);

  // WiFi connect with timeout
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - wifiStart >= WIFI_TIMEOUT_MS) goToSleep();
    delay(10);
    yield();
  }

  client.setServer(mqtt_server, mqtt_port);
  client.setKeepAlive(10);
  client.setSocketTimeout(5);
  client.setBufferSize(128);
  client.setCallback(Callback);

  reconnect();
  connectTime = millis(); // start "wait for command" timer
}

void reconnect() {
  unsigned long mqttStart = millis();
  while (!client.connected()) {
    if (millis() - mqttStart >= MQTT_TIMEOUT_MS) goToSleep();

    String clientId = "SmartBlind-";
    clientId += String(random(0xffff), HEX);

    if (client.connect(clientId.c_str(), mqtt_username, mqtt_password)) {
      int raw = analogRead(A0);
      raw = constrain(raw, 554, 750);
      int battPct = map(raw, 554, 750, 0, 100);
      battPct = constrain(battPct, 0, 100);
      client.publish(TOPIC_BATTERY, String(battPct).c_str(), true);
      client.endPublish();

      client.subscribe(TOPIC_ANGLE);
    } else {
      delay(100);
      yield();
    }
  }
}

void Callback(char* topic, byte* payload, unsigned int length) {
  commandReceived = true; // mark that we got a command

  String cmd;
  cmd.reserve(length);
  for (unsigned int i = 0; i < length; i++) cmd += (char)payload[i];
  uint32_t angle = (uint32_t)constrain(cmd.toInt(), 0, 180);

  // Load last angle from RTC
  RtcState st{};
  ESP.rtcUserMemoryRead(RTC_SLOT, (uint32_t*)&st, sizeof(st));
  bool rtcValid = (st.magic == RTC_MAGIC) && (st.checksum == rtcChecksum(st));
  uint32_t oldAngle = rtcValid ? st.angle : 255;

  if (oldAngle != angle) {
    digitalWrite(ServoEnablePin, HIGH);
    delay(120);
    servo.write(angle);
    delay(250);
    digitalWrite(ServoEnablePin, LOW);

    st.magic = RTC_MAGIC;
    st.angle = angle;
    st.checksum = rtcChecksum(st);
    ESP.rtcUserMemoryWrite(RTC_SLOT, (uint32_t*)&st, sizeof(st));
  }

  goToSleep();
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  // if no command received within MQTT_WAIT_CMD_MS after connect, go to sleep
  if (!commandReceived && (millis() - connectTime > MQTT_WAIT_CMD_MS)) {
    goToSleep();
  }
}
