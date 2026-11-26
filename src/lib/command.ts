import mqtt from "mqtt";

const ZONES = new Set(["flood", "trespass"]);

export async function stopBuzzer(zone: string) {
  if (!ZONES.has(zone)) return;

  const broker = process.env.MQTT_BROKER || "broker.hivemq.com";
  const port = process.env.MQTT_PORT || "1883";
  const topic = process.env.MQTT_CMD_TOPIC || "drukalert/zone/cmd";
  const message = `stop_buzzer ${zone}`;

  const client = mqtt.connect(`mqtt://${broker}:${port}`, {
    connectTimeout: 8_000,
    reconnectPeriod: 0,
  });

  await new Promise<void>((resolve, reject) => {
    const timer = setTimeout(() => {
      client.end(true);
      reject(new Error("The board did not accept the stop command."));
    }, 8_000);

    client.on("connect", () => {
      client.publish(topic, message, { qos: 1 }, (error) => {
        clearTimeout(timer);
        client.end(true);
        if (error) reject(error);
        else resolve();
      });
    });

    client.on("error", (error) => {
      clearTimeout(timer);
      client.end(true);
      reject(error);
    });
  });
}
