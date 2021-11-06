/* Author PricelessToolkit 
 *  
 * This Sketch uses fast connect Method. No DHCP no WIFI Search! 
 * 
 * All power measurements are taken with "Power Profiler Kit II" https://www.nordicsemi.com/Products/Development-hardware/Power-Profiler-Kit-2
 * 
 * One cycle connecting to wifi -> Publishing battery Percentage -> Subscribing to Topic
 * Comparing topic value with RTC memory value, last 1.5sec and uses "31.6uA Cycle"
 * 
 * 1 hours tests with 10min Cycle "Deep Sleep/Wake UP/Connect/Publish/Subscribe/Deep Sleep" uses 280uAh.
 * Blinds uses 900uA for every movements Open or Close.
 * 
 * How long will last 3000mA li-Ion 18650 battery? in perfect conditions  10714 hours or 446 deys -3.2 hours for every movement
 * I can say that these data are somewhat theoretical. So don't expect a miracle.
 * Autonomy depends on how many times you open and close the Blinds, WIFI signal strength!, Battery discharge rate, Temperature, Humidity, position of the moon and mars etc.
 * 
 * If you find this project useful or at least interesting, support me with SUBSCRIBING to my channel https://www.youtube.com/c/PricelessToolkit
 */

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <Servo.h>

#define STASSID "Change_This"  // ** your SSID
#define STAPSK  "Change_This"  // ** your password


uint8_t home_mac[6] = { 0xB2, 0xFF, 0xC8, 0xE2, 0xC2, 0xAA };    // ** The MAC address of your wifi router
int channel = 6;                                                 // ** The wifi channel of your wifi router

const char* ssid     = STASSID;
const char* password = STAPSK;

const char* mqtt_username = "Change_This";
const char* mqtt_password = "Change_This";
const char* mqtt_server = "Change_This";
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient client(espClient);

// change to suit your local network.  We don't use DHCP to save power!!!!!!
IPAddress local_IP(192, 168, 2, 200); // ** IP Adress of Blinds
IPAddress gateway(192, 168, 2, 1);    // ** Network gateway
IPAddress subnet(255, 255, 255, 0);   // ** Network subnet

uint32_t counter = 0;
uint32_t mqttcounter = 0;
int TimeToSleepSec = 600e6; // Cycle Time in milliseconds!  (600e6 = 600sec) 10min


Servo servo;
int ServoEnablePin = 4; // 5V DC-DC StepUp Pin.



void setup() {
  Serial.begin(115200);
  servo.attach(5);  // attaches the servo
  pinMode(ServoEnablePin, OUTPUT);
  digitalWrite(ServoEnablePin, LOW);

  // We start by connecting to a WiFi network
  
  WiFi.config(local_IP, gateway, subnet);

  if (!WiFi.config(local_IP, gateway, subnet)) {
   Serial.println("STA Failed to configure");
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password, channel, home_mac, true);


  while (WiFi.status() != WL_CONNECTED) {
    if (++counter > 1000){
      ESP.restart();
      
    }
    delay(10);
    // Serial.print(".");
  }


  client.setServer(mqtt_server, mqtt_port);
  reconnect();
  client.setCallback(Callback); 


  
}


void reconnect() {
  while (!client.connected()) {
    String clientId = "SmartBlind-";
    clientId += String(random(0xffff), HEX);

    // Attempt to connect
    if (client.connect(clientId.c_str(), mqtt_username, mqtt_password)) {
      int batt = map(analogRead(A0), 554, 750, 0, 100); // (analogRead(A2), MIN, MAX, 0, 100);
      
      //Serial.println("MQTT Connected");
      // Once connected, publish battery 0-100%...
      client.publish("SmartBlinds/Blind-1/Battery", String(batt).c_str());
      client.endPublish();
      // ... and resubscribe
      client.subscribe("SmartBlinds/Blind-1/Angle");

    }
    else
    {
      // Wait 0.1 seconds before retrying
     delay(100);
	 
     
    }
  }
}


void Callback(char* topic, byte* payload, unsigned int length) {
  String Command = "";

  for (int i = 0; i < length; i++) {
    Command = Command + (char)payload[i];
  }  
  
  uint32_t AngleValue = Command.toInt();

  uint32_t AngleValueOld;

  ESP.rtcUserMemoryRead(65, &AngleValueOld, sizeof(AngleValueOld));


  if(AngleValueOld != AngleValue) // after wake up checks if the new servo angle is a different then old angle value in RTC Memory 
  {

    digitalWrite(ServoEnablePin, HIGH); //Enables 5v DC-DC Step UP output for servo
    delay(100); // Delay for charging Capacitor
    servo.write(AngleValue); // Changes the angle of the servo
    ESP.rtcUserMemoryWrite(65, &AngleValue, sizeof(AngleValue)); // writes a new position in RTC Memory
    delay(250);
    
  }
  
  ESP.deepSleep(TimeToSleepSec);

}


void loop() {

    if (!client.connected()) 
        {
            reconnect();
        }
  client.loop();
}
