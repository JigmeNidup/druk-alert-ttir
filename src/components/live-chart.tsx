"use client";

import { useMemo, useState, type PointerEvent } from "react";
import { buildLiveChart, type ChartSample } from "@/lib/chart";
import { formatDistance, formatHumidity, formatStamp } from "@/lib/format";
import type { ReadingPoint } from "@/lib/types";

export function LiveChart({
  zoneId,
  name,
  points,
  showHumidity,
  startMs,
  endMs,
}: {
  zoneId: string;
  name: string;
  points: ReadingPoint[];
  showHumidity: boolean;
  startMs: number;
  endMs: number;
}) {
  const chart = useMemo(
    () => buildLiveChart(points, startMs, endMs, showHumidity),
    [points, startMs, endMs, showHumidity],
  );
  const [hover, setHover] = useState<ChartSample | null>(null);

  if (!chart) {
    return <p className="text-xs text-muted">The live graph appears as soon as a reading arrives.</p>;
  }

  const active = hover;
  const floor = chart.plot.y + chart.plot.height;

  function locate(event: PointerEvent<SVGRectElement>) {
    const bounds = event.currentTarget.ownerSVGElement?.getBoundingClientRect();
    if (!bounds || bounds.width === 0) return;
    const x = ((event.clientX - bounds.left) / bounds.width) * chart!.width;
    let nearest = chart!.samples[0];
    let best = Number.POSITIVE_INFINITY;
    for (const sample of chart!.samples) {
      const gap = Math.abs(sample.x - x);
      if (gap < best) {
        best = gap;
        nearest = sample;
      }
    }
    setHover(nearest);
  }

  const tipLeft = active ? (active.x / chart.width) * 100 : 0;
  const flip = active ? active.x > chart.width * 0.62 : false;

  return (
    <div className="relative">
      <svg
        viewBox={`0 0 ${chart.width} ${chart.height}`}
        className="aspect-[360/196] w-full"
        role="img"
        aria-label={`${name} live graph`}
      >
        <defs>
          <linearGradient id={`dist-${zoneId}`} x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%" stopColor="#7cb3ff" stopOpacity="0.32" />
            <stop offset="100%" stopColor="#7cb3ff" stopOpacity="0" />
          </linearGradient>
        </defs>

        {chart.distanceTicks.map((tick) => (
          <g key={`g-${tick.label}`}>
            <line
              x1={chart.plot.x}
              x2={chart.plot.x + chart.plot.width}
              y1={tick.y}
              y2={tick.y}
              stroke="#2c3d5c"
              strokeWidth="1"
            />
            <text
              x={chart.plot.x - 8}
              y={tick.y + 4}
              textAnchor="end"
              fill="#93a4bd"
              fontSize="12"
            >
              {tick.label}
            </text>
          </g>
        ))}

        {chart.humidityTicks.map((tick) => (
          <text
            key={`h-${tick.label}`}
            x={chart.plot.x + chart.plot.width + 8}
            y={tick.y + 4}
            fill="#fbbf24"
            fontSize="12"
          >
            {tick.label}
          </text>
        ))}

        {chart.distanceArea ? (
          <path d={chart.distanceArea} fill={`url(#dist-${zoneId})`} />
        ) : null}
        {chart.humidityPath ? (
          <path
            d={chart.humidityPath}
            fill="none"
            stroke="#fbbf24"
            strokeWidth="2"
            strokeLinejoin="round"
            strokeLinecap="round"
          />
        ) : null}
        {chart.distancePath ? (
          <path
            d={chart.distancePath}
            fill="none"
            stroke="#7cb3ff"
            strokeWidth="2.4"
            strokeLinejoin="round"
            strokeLinecap="round"
          />
        ) : null}

        {chart.samples.at(-1)?.distanceY != null ? (
          <circle
            cx={chart.samples.at(-1)!.x}
            cy={chart.samples.at(-1)!.distanceY!}
            r="3.5"
            fill="#7cb3ff"
          />
        ) : null}
        {showHumidity && chart.samples.at(-1)?.humidityY != null ? (
          <circle
            cx={chart.samples.at(-1)!.x}
            cy={chart.samples.at(-1)!.humidityY!}
            r="3.5"
            fill="#fbbf24"
          />
        ) : null}

        {active?.distanceY != null ? (
          <circle cx={active.x} cy={active.distanceY} r="4.5" fill="#7cb3ff" />
        ) : null}
        {active?.humidityY != null ? (
          <circle cx={active.x} cy={active.humidityY} r="4.5" fill="#fbbf24" />
        ) : null}

        {active ? (
          <line
            x1={active.x}
            x2={active.x}
            y1={chart.plot.y}
            y2={floor}
            stroke="#e7eef8"
            strokeOpacity="0.45"
            strokeDasharray="3 3"
          />
        ) : null}

        {chart.timeTicks.map((tick) => (
          <text
            key={`t-${tick.label}-${tick.x}`}
            x={tick.x}
            y={chart.height - 8}
            textAnchor={tick.anchor}
            fill="#93a4bd"
            fontSize="12"
          >
            {tick.label}
          </text>
        ))}

        <rect
          x={chart.plot.x}
          y={chart.plot.y}
          width={chart.plot.width}
          height={chart.plot.height}
          fill="transparent"
          onPointerMove={locate}
          onPointerLeave={() => setHover(null)}
        />
      </svg>

      {active ? (
        <div
          className="pointer-events-none absolute top-2 z-10 rounded-lg border border-edge bg-background/95 px-2.5 py-1.5 text-xs shadow-lg"
          style={{
            left: `${tipLeft}%`,
            transform: flip ? "translateX(calc(-100% - 8px))" : "translateX(8px)",
          }}
        >
          <p className="font-mono text-muted">{formatStamp(new Date(active.at))}</p>
          <p className="mt-1 font-mono text-accent">{formatDistance(active.distanceCm)}</p>
          {showHumidity ? (
            <p className="font-mono text-standby">{formatHumidity(active.humidityPct)}</p>
          ) : null}
        </div>
      ) : null}
    </div>
  );
}
