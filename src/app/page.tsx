import HomePage from "@/components/HomePage";

export const dynamic = "force-dynamic";

export default async function Home(): Promise<JSX.Element> {
  return <HomePage />;
}
