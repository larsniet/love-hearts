// /app/api/toggle-led/route.ts
import { NextRequest, NextResponse } from "next/server";
import mqttSingleton from "@/lib/mqttSingleton";

export async function POST(request: NextRequest): Promise<NextResponse> {
  const body = await request.json();
  console.log("API request received:", body);

  try {
    const { action } = body; // 'ON' or 'OFF'
    const topic = "home/lights/toggle";

    const response = await new Promise<NextResponse>((resolve) => {
      mqttSingleton.client.publish(
        topic,
        action,
        { qos: 1, retain: true },
        (error?: Error) => {
          if (error) {
            console.error("Error publishing MQTT message:", error);
            resolve(
              NextResponse.json(
                { success: false, error: "MQTT publish error" },
                { status: 500 }
              )
            );
          } else {
            // Update LED state
            mqttSingleton.isLedOn = action === "ON";
            resolve(NextResponse.json({ success: true }, { status: 200 }));
          }
        }
      );
    });

    return response;
  } catch (error) {
    console.error("Error in toggle-led API:", error);
    return NextResponse.json(
      { success: false, error: "Internal server error" },
      { status: 500 }
    );
  }
}
