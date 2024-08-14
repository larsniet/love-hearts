const espIp = process.env.ESP_IP ?? "192.168.0.72:8855";

export async function GET() {
  try {
    const response = await fetch(`http://${espIp}/status`, {
      cache: "no-store",
      next: { revalidate: 0 },
    });
    if (response.ok) {
      const data = await response.json();
      return new Response(JSON.stringify({ status: data.status }), {
        status: 200,
      });
    } else {
      return new Response(
        JSON.stringify({ error: "Failed to check LED status" }),
        {
          status: 500,
        }
      );
    }
  } catch (error) {
    return new Response(
      JSON.stringify({ error: "Error checking LED status" }),
      {
        status: 500,
      }
    );
  }
}
