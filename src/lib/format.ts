const stampOptions: Intl.DateTimeFormatOptions = {
  timeZone: "Asia/Thimphu",
  year: "numeric",
  month: "2-digit",
  day: "2-digit",
  hour: "2-digit",
  minute: "2-digit",
  second: "2-digit",
  hourCycle: "h23",
};

export function formatStamp(iso: string | Date) {
  const date = typeof iso === "string" ? new Date(iso) : iso;
  if (Number.isNaN(date.getTime())) return "—";

  const parts = new Intl.DateTimeFormat("en-GB", stampOptions).formatToParts(date);
  const pick = (type: Intl.DateTimeFormatPartTypes) =>
    parts.find((part) => part.type === type)?.value ?? "";

  return `${pick("year")}-${pick("month")}-${pick("day")} ${pick("hour")}:${pick("minute")}:${pick("second")}`;
}

export function formatClock(value: number | Date, withSeconds = false) {
  const date = typeof value === "number" ? new Date(value) : value;
  if (Number.isNaN(date.getTime())) return "—";
  const parts = new Intl.DateTimeFormat("en-GB", {
    timeZone: "Asia/Thimphu",
    hour: "2-digit",
    minute: "2-digit",
    second: withSeconds ? "2-digit" : undefined,
    hourCycle: "h23",
  }).formatToParts(date);
  const pick = (type: Intl.DateTimeFormatPartTypes) =>
    parts.find((part) => part.type === type)?.value ?? "";
  const clock = `${pick("hour")}:${pick("minute")}`;
  return withSeconds ? `${clock}:${pick("second")}` : clock;
}

export function formatDistance(value: number | null) {
  if (value == null) return "No reading";
  return `${value.toFixed(1)} cm`;
}

export function formatHumidity(value: number | null) {
  if (value == null) return "—";
  return `${value.toFixed(1)}%`;
}

export function formatMotion(value: boolean | null) {
  if (value == null) return "—";
  return value ? "Detected" : "Clear";
}
