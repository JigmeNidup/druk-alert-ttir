export const ZONES = [
  {
    id: "trespass",
    name: "TTIR Main Gate",
    hazard: "Trespass",
    kind: "gate",
    alertStatus: "Trespass detected",
    summary: "Motion and a person between 10 and 90 cm. The buzzer holds for 13 seconds.",
  },
  {
    id: "landslide",
    name: "Mountain Above TTIR Gate",
    hazard: "Landslide",
    kind: "slope",
    alertStatus: "Landslide hazard",
    summary: "A stone between 2 and 50 cm that shifts by 3 cm, or sits at 15 cm or closer. The buzzer holds for 9 seconds.",
  },
  {
    id: "flood",
    name: "River Alongside Railing",
    hazard: "Flood",
    kind: "water",
    alertStatus: "Flood hazard",
    summary: "Humidity at 40% or more, water between 4 and 60 cm, and a passing object. The buzzer holds for 5 seconds.",
  },
] as const;

export const SENSOR_OFFLINE = "Sensor offline";
export const AWAITING_TELEMETRY = "Awaiting telemetry";
export const CLEAR_STATUS = "Clear";

export type ZoneId = (typeof ZONES)[number]["id"];
export type ZoneKind = (typeof ZONES)[number]["kind"];

export function zoneById(id: string) {
  return ZONES.find((zone) => zone.id === id);
}
