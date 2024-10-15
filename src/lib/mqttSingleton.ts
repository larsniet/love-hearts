// /lib/mqttSingleton.ts
import mqtt, { MqttClient } from "mqtt";

// Define the interface for the MQTT Singleton
interface MqttSingleton {
  client: MqttClient;
  isLedOn: boolean;
}

declare global {
  // eslint-disable-next-line no-var -- Global augmentation
  var mqttSingleton: MqttSingleton | undefined;
}

if (!global.mqttSingleton) {
  // MQTT connection options
  const mqttHost: string = process.env.MQTT_HOST || "your-public-domain.com";
  const mqttPort: number = parseInt(process.env.MQTT_PORT || "8883", 10);
  const mqttUsername: string = process.env.MQTT_USERNAME || "yourusername";
  const mqttPassword: string = process.env.MQTT_PASSWORD || "yourpassword";

  const caFile = process.env.MQTT_CA_FILE;

  const options: mqtt.IClientOptions = {
    host: mqttHost,
    port: mqttPort,
    protocol: "mqtts",
    username: mqttUsername,
    password: mqttPassword,
    ca: caFile,
    rejectUnauthorized: false,
  };

  const client: MqttClient = mqtt.connect(options);

  const mqttSingleton: MqttSingleton = {
    client,
    isLedOn: false,
  };

  client.on("connect", () => {
    console.log("Connected to MQTT broker");

    const topic = "home/lights/toggle";
    client.subscribe(topic, { qos: 1 }, (error) => {
      if (error) {
        console.error("Subscribe error:", error);
      } else {
        console.log(`Subscribed to topic: ${topic}`);
      }
    });
  });

  client.on("error", (error) => {
    console.error("MQTT client error:", error);
  });

  client.on("message", (topic: string, message: Buffer) => {
    const msg = message.toString();
    console.log(`Received message on topic ${topic}: ${msg}`);
    if (topic === "home/lights/toggle") {
      if (msg === "ON") {
        mqttSingleton.isLedOn = true;
      } else if (msg === "OFF") {
        mqttSingleton.isLedOn = false;
      }
      console.log(
        `LED state updated to: ${mqttSingleton.isLedOn ? "on" : "off"}`
      );
    }
  });

  global.mqttSingleton = mqttSingleton;
}

export default global.mqttSingleton!;
