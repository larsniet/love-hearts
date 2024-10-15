// /app/api/check-led-status/route.ts
import { NextResponse } from "next/server";
import mqttSingleton from "@/lib/mqttSingleton";

export const dynamic = "force-dynamic";

export async function GET() {
  const isLedOn = mqttSingleton.isLedOn;
  console.log("LED status:", isLedOn);
  return NextResponse.json(
    {
      status: isLedOn ? "ON" : "OFF",
    },
    { status: 200 }
  );
}
