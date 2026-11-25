import { formatClock } from "./format";
import type { ReadingPoint } from "./types";

const WIDTH = 360;
const HEIGHT = 196;
const PLOT = { x: 40, y: 12, width: 276, height: 148 };

export type ChartSample = {
  x: number;
  at: number;
  distanceCm: number | null;
  humidityPct: number | null;
  distanceY: number | null;
  humidityY: number | null;
};

export type ChartTick = {
  y: number;
  label: string;
};

export type TimeTick = {
  x: number;
  label: string;
  anchor: "start" | "middle" | "end";
};

export type LiveChart = {
  width: number;
  height: number;
  plot: { x: number; y: number; width: number; height: number };
  distancePath: string;
  distanceArea: string;
  humidityPath: string;
  samples: ChartSample[];
  distanceTicks: ChartTick[];
  humidityTicks: ChartTick[];
  timeTicks: TimeTick[];
};

export function buildLiveChart(
  points: ReadingPoint[],
  startMs: number,
  endMs: number,
  showHumidity: boolean,
): LiveChart | null {
  const windowSpan = endMs - startMs;
  if (windowSpan <= 0) return null;

  const timed = points
    .map((point) => {
      const at = Date.parse(point.recordedAt);
      if (!Number.isFinite(at) || at < startMs || at > endMs + 2000) return null;
      return {
        at,
        distanceCm: point.distanceCm,
        humidityPct: showHumidity ? point.humidityPct : null,
      };
    })
    .filter((sample): sample is { at: number; distanceCm: number | null; humidityPct: number | null } => sample != null)
    .sort((left, right) => left.at - right.at);

  const view = viewRange(timed.map((sample) => sample.at), startMs, endMs);
  const span = view.end - view.start || 1;
  const draft = timed.map((sample) => {
    const ratio = Math.min(1, Math.max(0, (sample.at - view.start) / span));
    return {
      x: PLOT.x + ratio * PLOT.width,
      at: sample.at,
      distanceCm: sample.distanceCm,
      humidityPct: sample.humidityPct,
    };
  });

  const distances = draft
    .map((sample) => sample.distanceCm)
    .filter((value): value is number => value != null);
  const humidities = draft
    .map((sample) => sample.humidityPct)
    .filter((value): value is number => value != null);
  if (distances.length === 0 && humidities.length === 0) return null;

  const distance = scaleDistance(distances);
  const humidity = { min: 0, max: 100, ticks: [0, 50, 100] };
  const yDistance = (value: number) => yAt(value, distance.min, distance.max);
  const yHumidity = (value: number) => yAt(value, humidity.min, humidity.max);
  const samples: ChartSample[] = draft.map((sample) => ({
    ...sample,
    distanceY: sample.distanceCm == null ? null : yDistance(sample.distanceCm),
    humidityY: sample.humidityPct == null ? null : yHumidity(sample.humidityPct),
  }));
  const distanceLine = seriesPath(samples, "distanceCm", yDistance);
  const humidityLine = seriesPath(samples, "humidityPct", yHumidity);

  return {
    width: WIDTH,
    height: HEIGHT,
    plot: PLOT,
    distancePath: distanceLine.line,
    distanceArea: distanceLine.area,
    humidityPath: humidityLine.line,
    samples,
    distanceTicks: distance.ticks.map((value) => ({
      y: yDistance(value),
      label: tickLabel(value, distance.step),
    })),
    humidityTicks: showHumidity
      ? humidity.ticks.map((value) => ({
          y: yHumidity(value),
          label: String(value),
        }))
      : [],
    timeTicks: timeTicks(view.start, view.end),
  };
}

function viewRange(times: number[], startMs: number, endMs: number) {
  if (times.length === 0) return { start: startMs, end: endMs };
  const first = Math.min(...times);
  const last = Math.max(...times);
  const dataSpan = Math.max(last - first, 60_000);
  const windowSpan = endMs - startMs;
  if (dataSpan >= windowSpan * 0.6) return { start: startMs, end: endMs };
  const pad = Math.max(dataSpan * 0.12, 20_000);
  return {
    start: Math.max(startMs, first - pad),
    end: Math.min(endMs, last + pad),
  };
}

function yAt(value: number, min: number, max: number) {
  const span = max - min || 1;
  const ratio = (value - min) / span;
  return PLOT.y + PLOT.height - ratio * PLOT.height;
}

function seriesPath(
  samples: ChartSample[],
  key: "distanceCm" | "humidityPct",
  yOf: (value: number) => number,
) {
  let line = "";
  let area = "";
  let run: { x: number; y: number }[] = [];

  const flush = () => {
    if (run.length > 1) {
      const drawn = run
        .map((point, index) => `${index === 0 ? "M" : "L"}${point.x.toFixed(1)} ${point.y.toFixed(1)}`)
        .join(" ");
      line = line ? `${line} ${drawn}` : drawn;
      const floor = PLOT.y + PLOT.height;
      const last = run[run.length - 1];
      const first = run[0];
      area = `${area} ${drawn} L${last.x.toFixed(1)} ${floor} L${first.x.toFixed(1)} ${floor} Z`;
    }
    run = [];
  };

  for (const sample of samples) {
    const value = sample[key];
    if (value == null) {
      flush();
      continue;
    }
    run.push({ x: sample.x, y: yOf(value) });
  }
  flush();
  return { line, area: area.trim() };
}

function scaleDistance(values: number[]) {
  if (values.length === 0) return { min: 0, max: 40, step: 10, ticks: [0, 10, 20, 30, 40] };
  let min = Math.min(...values);
  let max = Math.max(...values);
  if (max - min < 8) {
    const mid = (max + min) / 2;
    min = mid - 6;
    max = mid + 6;
  } else {
    const pad = (max - min) * 0.14;
    min -= pad;
    max += pad;
  }
  if (min < 0) min = 0;
  return niceScale(min, max);
}

function niceScale(min: number, max: number) {
  const rough = Math.max(max - min, 1) / 4;
  const power = 10 ** Math.floor(Math.log10(rough));
  const error = rough / power;
  const step = (error >= 7.5 ? 10 : error >= 3.5 ? 5 : error >= 1.5 ? 2 : 1) * power;
  const niceMin = Math.floor(min / step) * step;
  const niceMax = Math.max(Math.ceil(max / step) * step, niceMin + step);
  const ticks: number[] = [];
  for (let value = niceMin; value <= niceMax + step * 0.001; value += step) {
    ticks.push(Number(value.toFixed(4)));
    if (ticks.length > 6) break;
  }
  return { min: niceMin, max: niceMax, step, ticks };
}

function tickLabel(value: number, step: number) {
  if (step >= 1) return String(Math.round(value));
  return value.toFixed(1);
}

function timeTicks(startMs: number, endMs: number): TimeTick[] {
  const span = endMs - startMs;
  const marks = span < 5 * 60 * 1000 ? [0, 0.5, 1] : [0, 0.33, 0.66, 1];
  const withSeconds = span < 5 * 60 * 1000;
  return marks.map((ratio, index) => ({
    x: PLOT.x + ratio * PLOT.width,
    label: formatClock(startMs + span * ratio, withSeconds),
    anchor: index === 0 ? "start" : index === marks.length - 1 ? "end" : "middle",
  }));
}
