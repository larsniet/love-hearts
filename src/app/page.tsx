import HomePage from "@/components/HomePage";
const baseUrl = process.env.NEXT_PUBLIC_BASE_URL ?? "http://localhost:3000";

export default async function Home(): Promise<JSX.Element> {
  let isReachable = false;
  let isLedOn = false;

  try {
    const response = await fetch(`${baseUrl}/api/check-reachability`);
    const data = await response.json();
    isReachable = data.reachable;
  } catch (error) {
    console.error("Error checking reachability:", error);
  }

  try {
    const response = await fetch(`${baseUrl}/api/check-led-status`);
    if (response.ok) {
      const data = await response.json();
      isLedOn = data.status === "on";
    }
  } catch (error) {
    console.error("Error checking LED status:", error);
  }

  return (
    <main className="flex min-h-screen flex-col items-center justify-center p-24">
      <HomePage initialReachable={isReachable} initialLedOn={isLedOn} />
    </main>
  );
}
