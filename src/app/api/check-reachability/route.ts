export const dynamic = "force-dynamic";

const espIp = process.env.ESP_IP ?? "192.168.0.72:8855";

export async function GET() {
  try {
    const response = await fetch(`http://${espIp}/`, {
      cache: "no-store",
      next: { revalidate: 5 },
    });
    if (response.ok) {
      return new Response(JSON.stringify({ reachable: true }), {
        status: 200,
      });
    } else {
      return new Response(JSON.stringify({ reachable: false }), {
        status: 200,
      });
    }
  } catch (error) {
    return new Response(JSON.stringify({ reachable: false }), {
      status: 500,
    });
  }
}
