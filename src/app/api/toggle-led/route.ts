// /app/api/toggle-led/route.ts
import { NextRequest, NextResponse } from "next/server";
import mqttSingleton from "@/lib/mqttSingleton";

export async function POST(request: NextRequest) {
  const body = await request.json();
  console.log("API request received:", body);

  try {
    const { action } = body; // 'ON' or 'OFF'
    const topic = "home/lights/toggle";

    return new Promise((resolve) => {
      mqttSingleton.client.publish(
        topic,
        action,
        { qos: 1 },
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
  } catch (error) {
    console.error("Error in toggle-led API:", error);
    return NextResponse.json(
      { success: false, error: "Internal server error" },
      { status: 500 }
    );
  }
}
