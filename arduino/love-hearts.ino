#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <WiFiManager.h>

// MQTT broker details
const char* mqtt_server = "<mqtt-server-ip>";
const int mqtt_port = "<mqtt-server-port>";

// MQTT authentication
const char* mqtt_user = "<mqtt-username>";
const char* mqtt_password = "<mqtt-password>";

// Unique client ID for each ESP32
const char* client_id = "<client-id-1>";

// MQTT topic to publish and subscribe
const char* mqtt_topic = "<mqtt-topic>";

const char* ca_cert = \
"-----BEGIN CERTIFICATE-----\n" \
"............................................\n" \
"-----END CERTIFICATE-----\n";

WiFiClientSecure espClient;
PubSubClient client(espClient);
WiFiManager wm; 

const int buttonPin = 12;  // GPIO pin connected to the button
const int lightPin = 5;    // GPIO pin connected to the light
bool lightState = false;   // Current state of the light

// Variables for button press handling
unsigned long buttonPressTime = 0;
bool buttonHeld = false;
bool syncingMode = false;

// Variables for blinking light
unsigned long previousMillis = 0;
const long interval = 500; 
bool ledState = LOW;

void setup() {
  Serial.begin(115200);

  // Initialize GPIO pins
  pinMode(buttonPin, INPUT_PULLUP);  // Button with internal pull-up resistor
  pinMode(lightPin, OUTPUT);
  digitalWrite(lightPin, LOW);  // Light off initially

  // Connect to Wi-Fi
  setup_wifi();

  // Configure MQTT client
  espClient.setCACert(ca_cert);  // Load CA certificate
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);
}

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to Wi-Fi... ");

  // Attempt to connect using saved credentials
  WiFi.begin();

  unsigned long startAttemptTime = millis();

  // Try to connect for 10 seconds
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Connected to Wi-Fi");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Failed to connect to Wi-Fi");
    // Optionally, you can start the configuration portal here
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived on topic: ");
  Serial.println(topic);

  // Convert payload to string
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.print("Message: ");
  Serial.println(message);

  // Toggle the light based on the message
  if (message == "ON") {
    lightState = true;
    digitalWrite(lightPin, HIGH);
  } else if (message == "OFF") {
    lightState = false;
    digitalWrite(lightPin, LOW);
  }
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection... ");
    // Attempt to connect
    if (client.connect(client_id, mqtt_user, mqtt_password)) {
      Serial.println("connected");
      // Once connected, subscribe
      client.subscribe(mqtt_topic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" - trying again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void startSyncingMode() {
  Serial.println("Entering syncing mode");

  // Reset saved Wi-Fi credentials
  wm.resetSettings();

  // Set the portal to non-blocking
  wm.setConfigPortalBlocking(false);

  // Start the configuration portal
  wm.startConfigPortal("<device-name>");

  // Indicate we're in syncing mode
  syncingMode = true;
}

void blinkLight() {
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    // Toggle the LED
    ledState = !ledState;
    digitalWrite(lightPin, ledState ? HIGH : LOW);
  }
}

void loop() {
  // Read the button state
  int buttonState = digitalRead(buttonPin);

  // Handle button press
  if (buttonState == LOW) {
    if (!buttonHeld) {
      // Button was just pressed
      buttonHeld = true;
      buttonPressTime = millis();
    } else {
      // Button is being held
      if (millis() - buttonPressTime > 5000) {
        // Button held for more than 5 seconds
        if (!syncingMode) {
          // Enter syncing mode
          startSyncingMode();
        }
      }
    }
  } else {
    if (buttonHeld) {
      // Button was just released
      buttonHeld = false;
      if (!syncingMode && (millis() - buttonPressTime < 5000)) {
        // Short press, toggle the light
        Serial.println("Button pressed");
        // Toggle the light state
        lightState = !lightState;
        // Update the light
        digitalWrite(lightPin, lightState ? HIGH : LOW);
        // Publish the new state
        if (client.connected()) {
          if (lightState) {
            client.publish(mqtt_topic, "ON", true);
          } else {
            client.publish(mqtt_topic, "OFF", true);
          }
        }
      }
    }
  }

  // If in syncing mode, process WiFiManager and blink the light
  if (syncingMode) {
    wm.process();     // Process WiFiManager portal
    blinkLight();     // Blink the light

    // Check if Wi-Fi is connected
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Connected to new Wi-Fi network");
      syncingMode = false;

      // Reconnect MQTT client
      reconnect();
    }
  } else {
    // Ensure the light reflects the current state when not in syncing mode
    digitalWrite(lightPin, lightState ? HIGH : LOW);

    // Handle MQTT communication if Wi-Fi is connected
    if (WiFi.status() == WL_CONNECTED) {
      if (!client.connected()) {
        reconnect();
      }
      client.loop();
    } else {
      // Wi-Fi is not connected
      // Optionally, inform the user or attempt to reconnect
    }
  }
}
