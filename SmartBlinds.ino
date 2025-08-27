/* Smart Blind (ESP8266) — lean debug + boot state publish
 * - Fast Wi-Fi connect (fixed BSSID + channel + static IP)
 * - Timings: Wi-Fi + MQTT connect durations
 * - Publishes current Position (0–100%) on boot from RTC
 * - Accepts PositionSet (0–100%), converts to 0–180° servo (0°=closed, 180°=open)
 * - Deep sleep after command or after 5 s with no command
 */

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <Servo.h>

// ===== Wi-Fi (fast-connect) =====
#define STASSID "Change-This"
#define STAPSK  "Change-This"
uint8_t home_mac[6] = { 0x94, 0xA6, 0x7E, 0x94, 0x9B, 0x81 };
int channel = 1;
const char* ssid     = STASSID;
const char* password = STAPSK;

IPAddress local_IP(Change-This);
IPAddress gateway (Change-This);
IPAddress subnet  (Change-This);

// ===== MQTT =====
const char* mqtt_username = "Change-This";
const char* mqtt_password = "Change-This";
const char* mqtt_server   = "Change-This";
const int   mqtt_port     = 1883;

WiFiClient espClient;
PubSubClient client(espClient);

// Topics
const char* TOPIC_BATTERY = "SmartBlinds/Blind-1/Battery";
const char* TOPIC_POS_SET = "SmartBlinds/Blind-1/PositionSet"; // 0..100 (command)
const char* TOPIC_POS     = "SmartBlinds/Blind-1/Position";    // 0..100 (state)
const char* TOPIC_AVAIL   = "SmartBlinds/Blind-1/availability";
const char* AVAIL_ON  = "online";
const char* AVAIL_OFF = "offline";

// ===== Power / Timing =====
static const uint64_t TimeToSleepUs     = 600ULL * 1000000ULL; // 10 min
static const unsigned long WIFI_TIMEOUT_MS   = 15000UL;
static const unsigned long MQTT_TIMEOUT_MS   = 15000UL;
static const unsigned long MQTT_WAIT_CMD_MS  = 5000UL;

// ===== Hardware =====
Servo servo;
const int ServoPin       = 5;  // D1
const int ServoEnablePin = 4;  // D2 (step-up enable, HIGH=on)

// ===== RTC cache =====
struct RtcState {
  uint32_t magic;
  uint32_t angle;     // 0..180
  uint32_t checksum;
};
static const uint32_t RTC_MAGIC = 0xB1A9D501;
static const uint32_t RTC_SLOT  = 65;
static uint32_t rtcChecksum(const RtcState& s) { return (s.magic ^ s.angle) ^ 0xA5A5A5A5; }

// ===== Globals =====
bool commandReceived = false;
unsigned long connectTime = 0;
unsigned long t_boot_ms, t_wifi_begin_ms, t_wifi_connected_ms;
unsigned long t_mqtt_begin_ms, t_mqtt_connected_ms;

// ===== Helpers (fixed mapping: 0%->0°, 100%->180°) =====
static uint8_t angleToPercent(uint32_t a) {
  if (a > 180) a = 180;
  return (uint8_t)((a * 100UL) / 180UL);  // 0° -> 0%, 180° -> 100%
}
static uint32_t percentToAngle(uint8_t p) {
  if (p > 100) p = 100;
  return (uint32_t)((p * 180UL) / 100UL); // 0% -> 0°, 100% -> 180°
}
static void printIp(IPAddress ip) { Serial.printf("%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]); }

void goToSleep(const char* reason) {
  Serial.printf("[SLEEP] %lus — %s\n", (unsigned long)(TimeToSleepUs / 1000000ULL), reason);
  digitalWrite(ServoEnablePin, LOW);
  servo.detach();
  if (client.connected()) client.disconnect();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(10);
  Serial.flush();
  ESP.deepSleep(TimeToSleepUs, WAKE_RF_DEFAULT);
}

// Publish availability + current Position (from RTC) at boot
void publishInitialState() {
  client.publish(TOPIC_AVAIL, AVAIL_ON, true);

  RtcState st{};
  ESP.rtcUserMemoryRead(RTC_SLOT, (uint32_t*)&st, sizeof(st));
  bool ok = (st.magic == RTC_MAGIC) && (st.checksum == rtcChecksum(st));
  uint8_t pos = ok ? angleToPercent(st.angle) : 0;
  client.publish(TOPIC_POS, String(pos).c_str(), true);
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("=== SMART BLIND BOOT ===");
  t_boot_ms = millis();

  pinMode(ServoEnablePin, OUTPUT);
  digitalWrite(ServoEnablePin, LOW);
  servo.attach(ServoPin);

  WiFi.persistent(false);
  WiFi.config(local_IP, gateway, subnet);
  WiFi.mode(WIFI_STA);

  t_wifi_begin_ms = millis();
  WiFi.begin(ssid, password, channel, home_mac, true);

  unsigned long wifiStart = millis();
  Serial.println("[WiFi] Connecting...");
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - wifiStart >= WIFI_TIMEOUT_MS) goToSleep("Wi-Fi timeout");
    delay(10);
    yield();
  }
  t_wifi_connected_ms = millis();
  Serial.print("[WiFi] Connected. IP: "); printIp(WiFi.localIP());
  Serial.printf("  RSSI: %d dBm  (dur %lums)\n", WiFi.RSSI(), t_wifi_connected_ms - t_wifi_begin_ms);

  client.setServer(mqtt_server, mqtt_port);
  client.setKeepAlive(10);
  client.setSocketTimeout(5);
  client.setBufferSize(512);
  client.setCallback(mqttCallback);

  mqttReconnect();           // connect + subscribe
  publishInitialState();     // availability + current position (retained)
  connectTime = millis();    // start short wait window
  Serial.println("[INFO] Ready for PositionSet (0–100).");
}

void mqttReconnect() {
  t_mqtt_begin_ms = millis();
  unsigned long start = millis();
  while (!client.connected()) {
    if (millis() - start >= MQTT_TIMEOUT_MS) goToSleep("MQTT timeout");

    String clientId = "SmartBlind-" + String(ESP.getChipId(), HEX);
    Serial.printf("[MQTT] Connecting as %s ...\n", clientId.c_str());

    bool ok = client.connect(
      clientId.c_str(),
      mqtt_username, mqtt_password,
      TOPIC_AVAIL, 0, true, AVAIL_OFF
    );

    if (ok) {
      t_mqtt_connected_ms = millis();
      Serial.printf("[MQTT] Connected. (dur %lums, boot→MQTT %lums)\n",
                    t_mqtt_connected_ms - t_mqtt_begin_ms,
                    t_mqtt_connected_ms - t_boot_ms);

      // Battery (retained)
      int raw = analogRead(A0);
      raw = constrain(raw, 554, 750);
      int battPct = map(raw, 554, 750, 0, 100);
      battPct = constrain(battPct, 0, 100);
      client.publish(TOPIC_BATTERY, String(battPct).c_str(), true);

      client.subscribe(TOPIC_POS_SET); // listen for 0..100%
    } else {
      delay(100);
      yield();
    }
  }
}

// Handle PositionSet (0..100) -> move servo 0..180
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (String(topic) != TOPIC_POS_SET) return;
  commandReceived = true;

  String msg; msg.reserve(length);
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  uint8_t percent = (uint8_t)constrain(msg.toInt(), 0, 100);
  uint32_t desiredAngle = percentToAngle(percent);
  Serial.printf("[CMD] %u%% -> %lu°\n", percent, desiredAngle);

  // Read last angle
  RtcState st{};
  ESP.rtcUserMemoryRead(RTC_SLOT, (uint32_t*)&st, sizeof(st));
  bool rtcValid = (st.magic == RTC_MAGIC) && (st.checksum == rtcChecksum(st));
  uint32_t lastAngle = rtcValid ? st.angle : 255;
  Serial.printf("[RTC] valid=%s last=%lu°\n", rtcValid ? "YES" : "NO", lastAngle);

  if (lastAngle != desiredAngle) {
    Serial.println("[SERVO] Enable rail + move");
    digitalWrite(ServoEnablePin, HIGH);
    delay(120);
    servo.write(desiredAngle);
    delay(250);
    digitalWrite(ServoEnablePin, LOW);
    Serial.println("[SERVO] Done, rail off");

    st.magic = RTC_MAGIC;
    st.angle = desiredAngle;
    st.checksum = rtcChecksum(st);
    ESP.rtcUserMemoryWrite(RTC_SLOT, (uint32_t*)&st, sizeof(st));
  } else {
    Serial.println("[SERVO] Same angle, skip");
  }

  // Publish final position so HA reflects it
  uint8_t currentPos = angleToPercent(desiredAngle);
  client.publish(TOPIC_POS, String(currentPos).c_str(), true);

  goToSleep("command handled");
}

void loop() {
  if (!client.connected()) mqttReconnect();
  client.loop();

  // Short window to catch retained or quick manual commands
  if (!commandReceived && (millis() - connectTime > MQTT_WAIT_CMD_MS)) {
    goToSleep("no command");
  }
}
