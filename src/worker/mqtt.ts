import mqtt from "mqtt";
import { prisma } from "../lib/db";
import {
  ALERT_STARTED,
  RESOLVED_ACKNOWLEDGED,
  RESOLVED_UNACKNOWLEDGED,
} from "../lib/events";
import { ingestTelemetry } from "../lib/ingest";
import { sendTelegramAlert, telegramEnabled } from "../lib/telegram";
import {
  distanceSample,
  flag,
  humiditySample,
  ingestAuthorized,
  isEspPayload,
  parseTelemetry,
} from "../lib/telemetry";
import type { RaisedAlert } from "../lib/types";
import { zoneById } from "../lib/zones";

const broker = process.env.MQTT_BROKER || "broker.hivemq.com";
const port = Number(process.env.MQTT_PORT || 1883);
const topic = process.env.MQTT_TOPIC || "drukalert/zone/telemetry";

if (!process.env.DATABASE_URL) {
  console.error("DATABASE_URL is not set. Copy .env.example to .env and start Postgres.");
  process.exit(1);
}

const client = mqtt.connect(`mqtt://${broker}:${port}`, {
  clientId: `drukalert-console-${Math.random().toString(16).slice(2, 10)}`,
  reconnectPeriod: 3000,
  connectTimeout: 10_000,
});

let chain: Promise<void> = Promise.resolve();
const telegramSent = new Set<string>();

client.on("connect", () => {
  console.log(`[mqtt] Connected to ${broker}:${port}`);
  console.log(
    telegramEnabled()
      ? "[mqtt] Telegram alerts are enabled."
      : "[mqtt] Telegram alerts are off. Set TELEGRAM_BOT_TOKEN and TELEGRAM_CHAT_ID to enable them.",
  );
  client.subscribe(topic, { qos: 1 }, (error) => {
    if (error) {
      console.error("[mqtt] Subscribe failed.", error);
      return;
    }
    console.log(`[mqtt] Listening on ${topic}`);
    if (!process.env.MQTT_INGEST_KEY?.trim()) {
      console.error("[mqtt] MQTT_INGEST_KEY is not set. Telemetry will be refused.");
    }
  });
});

client.on("message", (_topic, payload) => {
  chain = chain
    .then(() => handleMessage(payload.toString("utf8")))
    .catch((error) => {
      console.error("[mqtt] Failed to store telemetry.", error);
    });
});

client.on("error", (error) => {
  console.error("[mqtt]", error.message);
});

client.on("reconnect", () => {
  console.log("[mqtt] Reconnecting...");
});

async function handleMessage(raw: string) {
  const payload = parseTelemetry(raw);
  if (!payload || !isEspPayload(payload)) {
    console.warn("[mqtt] Ignored a packet that is not ESP32 zone telemetry.");
    return;
  }
  if (!ingestAuthorized(payload)) {
    console.warn("[mqtt] Ignored a packet with a missing or wrong ingest key.");
    return;
  }

  let raisedZones: string[] = [];
  try {
    const raised = await ingestTelemetry(payload);
    raisedZones = raised.map((alert) => alert.zone);
  } catch (error) {
    console.error("[mqtt] Failed to store telemetry.", error);
  }

  const flood = payload.flood_alert ?? 0;
  const trespass = payload.trespass_alert ?? 0;
  console.log(
    `[mqtt] flood=${flood} trespass=${trespass}${
      raisedZones.length ? ` new=${raisedZones.join(",")}` : ""
    }`,
  );

  await deliverOpenAlerts(payload);
}

const ACTIVE_ZONES = [
  ["flood", "flood_alert", "flood_dist", "flood_hum"],
  ["trespass", "trespass_alert", "trespass_dist", ""],
] as const;

async function deliverOpenAlerts(payload: Record<string, unknown>) {
  for (const [zone, alertKey, distanceKey, humidityKey] of ACTIVE_ZONES) {
    if (!flag(payload[alertKey])) {
      telegramSent.delete(`${zone}:live`);
      continue;
    }

    const open = await openAlertId(zone);
    const key = open ?? `${zone}:live`;
    if (telegramSent.has(key)) continue;

    try {
      const delivered = await sendTelegramAlert(alertFor(zone, distanceKey, humidityKey, payload));
      if (!delivered) continue;
      telegramSent.add(key);
      console.log(`[telegram] Sent ${zone}.`);
    } catch (error) {
      console.error("[telegram]", error instanceof Error ? error.message : "Send failed.");
    }
  }
}

async function openAlertId(zone: string) {
  try {
    const started = await prisma.hazardEvent.findFirst({
      where: { zone, action: ALERT_STARTED },
      orderBy: { createdAt: "desc" },
    });
    if (!started) return null;
    const resolved = await prisma.hazardEvent.findFirst({
      where: {
        zone,
        createdAt: { gt: started.createdAt },
        action: { in: [RESOLVED_ACKNOWLEDGED, RESOLVED_UNACKNOWLEDGED] },
      },
    });
    return resolved ? null : started.id;
  } catch (error) {
    console.error("[telegram] Could not read the open alert.", error instanceof Error ? error.message : "");
    return null;
  }
}

function alertFor(
  zone: string,
  distanceKey: string,
  humidityKey: string,
  payload: Record<string, unknown>,
): RaisedAlert {
  const meta = zoneById(zone);
  return {
    zone,
    zoneName: meta?.name ?? zone,
    status: meta?.alertStatus ?? "Alert",
    distanceCm: distanceSample(payload[distanceKey]).value,
    humidityPct: humidityKey ? humiditySample(payload[humidityKey]).value : null,
  };
}

async function shutdown() {
  client.end(true);
  await prisma.$disconnect();
  process.exit(0);
}

process.on("SIGINT", () => {
  void shutdown();
});
process.on("SIGTERM", () => {
  void shutdown();
});
