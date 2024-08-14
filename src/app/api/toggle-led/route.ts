export const dynamic = "force-dynamic";

const espIp = process.env.ESP_IP ?? "192.168.0.72:8855";

export async function GET() {
  try {
    const response = await fetch(`http://${espIp}/toggle`, {
      cache: "no-store",
      next: { revalidate: 0 },
    });
    if (response.ok) {
      return new Response(JSON.stringify({ success: true }), {
        status: 200,
      });
    } else {
      return new Response(JSON.stringify({ error: "Failed to toggle LED" }), {
        status: 500,
      });
    }
  } catch (error) {
    return new Response(JSON.stringify({ error: "Error toggling LED" }), {
      status: 500,
    });
  }
}
