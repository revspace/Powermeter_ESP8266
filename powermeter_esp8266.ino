/*
Powermeter 2.0 for ESP8266. revspace.nl/PowerMeter

Connect each S0-output as follows:

-3.3v
-ESP pin + 10k pulldown to GND

Define everything in the configuration below

Done for RevSpace, the Hague Hackerspace (revspace.nl)
Original code by Smeding, port and adaptation to ESP8266 by Sebastius.

Improvements welcome, the main loop should probably be interrupt based...

*/

// for ULONG_MAX
#include <limits.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>

// WiFi settings, feel free to edit
const char ssid[] = "revspace-pub-2.4ghz";                  //  your network SSID (name)
const char pass[] = "";                                     // your network password
const char* mqtt_server = "mosquitto.space.revspace.nl";    

// Configuration, feel free to edit
char* mqtt_id = "powermeter_01";                            // Must be unique!
int zone = 2;                                               // See revspace.nl/espsensorgrid
int inputPins[] = { 5, 4, 2 };    
char* meterNames[] = {"LK5-9-19", "LK5-9-26", "LK5-9-13"};  // Must be same amount of entries as inputPins!
const int BAUD_RATE   = 115200;                             // serial baud rate
const int DEBOUNCE_INTERVAL = 30;                           // debounce interval in ms
const float PULSE_ENERGY    = 1.8e6;                        // energy per pulse in W*ms; 1.8MW*ms = 0.5W*h

// Automatically count number of inputs
#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))
const int  NUM_INPUTS = ARRAY_SIZE( inputPins);

// global state
// state for each S0 input
struct counter {
  // for debouncing
  bool    outValue;                                         // debounced value
  bool    lastValue;                                        // last actual pin value
  unsigned long debounceTime;                               // time lastValue has been stable for

  // for tracking time between pulses
  bool    initialized;                                      // true when lastTime is valid
  unsigned long lastTime;                                   // last time
} counters[NUM_INPUTS];

// MQTT stuff
WiFiClient espClient;
PubSubClient client(mqtt_server, 1883, onMqttMessage, espClient);
long lastMsg = 0;
char msg[50];
int value = 0;
long lastReconnectAttempt = 0;

void setup() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  Serial.begin(BAUD_RATE);
  Serial.println();
  Serial.println("PowerMeter 2.0");

  // the relevant pins are inputs by default, so that's fine
  unsigned long time = millis();
  for (int i = 0; i < NUM_INPUTS; i++) {
    pinMode (inputPins[i], INPUT);
    counters[i].outValue  = true;
    counters[i].lastValue = true;
    counters[i].debounceTime = time;
    counters[i].initialized = false;
    counters[i].lastTime  = 0;
  }

  // We start by connecting to a WiFi network
  Serial.print("Connecting to ");

  Serial.println(ssid);
  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED) {
    delay(50);
    Serial.print(".");
  }
  Serial.println("");

  Serial.print("WiFi connected to: ");
  Serial.println(ssid);

  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println();
}

void loop() {
  int d = 5;
  unsigned long time = millis();

  for (int i = 0; i < NUM_INPUTS; i++) {
    bool v = digitalRead(inputPins[i]);

    if (v != counters[i].lastValue)
      counters[i].debounceTime = time;

    if ((time - counters[i].debounceTime) > DEBOUNCE_INTERVAL) {
      if (v != counters[i].outValue) {
        counters[i].outValue = v;
        if (!counters[i].outValue) {      // falling edge
          if (counters[i].initialized) {    // not the first falling edge, so we have a reference
            float power;
            if (time < counters[i].lastTime) { // check for overflow since lastTime
              // we assume it's only overflowed once... more than 50 days between pulses seems a bit silly :)
              power = PULSE_ENERGY / ((float)ULONG_MAX - ((float)time - (float)counters[i].lastTime));
            } else {
              power = PULSE_ENERGY / ((float)time - (float)counters[i].lastTime);
            }

            Serial.print(meterNames[i]);
            Serial.print(": ");
            Serial.print(power);
            Serial.println("W");

            String topic = "revspace/sensors/power/" + String(zone) + "/" + meterNames[i];
            String message = String(power) + " W";
            mqtt_publish(topic, message, 1);

          }

          counters[i].lastTime  = time;
          counters[i].initialized = true;
        }
      }
    }
    counters[i].lastValue = v;
  }
}

boolean reconnect() {
  if (client.connect(mqtt_id)) {
    Serial.println("Reconnected to MQTT");
    // Once connected, publish an announcement...
    String topic = "revspacesensorgrid/power/" + String(zone) + "/" + mqtt_id;
    String message = "Reconnect";
    mqtt_publish (topic, message, 1);
    client.loop();
  }
  return client.connected();
}

void onMqttMessage(char* topic, byte* payload, unsigned int length) {

}

void mqtt_publish (String topic, String message, bool retain) {
  Serial.println();
  Serial.print("Publishing ");
  Serial.print(message);
  Serial.print(" to ");
  Serial.println(topic);
  Serial.println();

  char t[100], m[100];
  topic.toCharArray(t, sizeof t);
  message.toCharArray(m, sizeof m);
  if (client.connected()) {
    // Client connected
    client.loop();
  } else {
    Serial.println(".");
    long verstreken = millis();
    if (verstreken - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = verstreken;
      // Attempt to reconnect
      if (reconnect()) {
        lastReconnectAttempt = 0;
      }
    }
  }
  client.publish(t, m, retain);
}



