import { NextResponse } from "next/server";
import { getSession } from "@/lib/auth";
import { stopBuzzer } from "@/lib/command";

const ZONES = new Set(["flood", "landslide", "trespass"]);

export async function POST(request: Request) {
  const session = await getSession();
  if (!session) {
    return NextResponse.json({ error: "Unauthorized" }, { status: 401 });
  }

  const body = (await request.json().catch(() => null)) as { zone?: unknown } | null;
  const zone = typeof body?.zone === "string" ? body.zone : "";
  if (!ZONES.has(zone)) {
    return NextResponse.json({ error: "Unknown zone." }, { status: 400 });
  }

  try {
    await stopBuzzer(zone);
  } catch {
    return NextResponse.json({ error: "The board did not get the stop command." }, { status: 502 });
  }

  return NextResponse.json({ ok: true });
}
