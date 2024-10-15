import HomePage from "@/components/HomePage";

export const dynamic = "force-dynamic";

export default async function Home(): Promise<JSX.Element> {
  return (
    <main className="flex min-h-screen flex-col items-center justify-center p-24">
      <HomePage />
    </main>
  );
}
