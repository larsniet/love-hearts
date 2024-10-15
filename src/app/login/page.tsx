// app/login/page.tsx
"use client";

import { useState } from "react";
import { useRouter } from "next/navigation";

export default function LoginPage() {
  const [isLoading, setIsLoading] = useState(false);
  const [password, setPassword] = useState("");
  const router = useRouter();
  const [error, setError] = useState("");

  const handleLogin = async (e: React.FormEvent) => {
    e.preventDefault();
    setIsLoading(true);

    try {
      const res = await fetch("/api/login", {
        method: "POST",
        body: JSON.stringify({ password }),
        headers: {
          "Content-Type": "application/json",
        },
      });

      if (res.ok) {
        router.push("/");
      } else {
        setError("Invalid password");
      }
    } catch (error) {
      console.error(error);
    } finally {
      setIsLoading(false);
    }
  };

  return (
    <form onSubmit={handleLogin} className="flex flex-col items-center">
      <h1 className="text-2xl font-bold mb-4">Login</h1>
      <input
        type="password"
        placeholder="Enter password"
        value={password}
        onChange={(e) => {
          setError("");
          setPassword(e.target.value);
        }}
        className="mb-4 p-2 border border-gray-300 rounded-md"
      />
      <button
        type="submit"
        disabled={isLoading}
        className={`p-2 text-white w-full bg-blue-500 rounded-md ${
          isLoading ? "opacity-50 cursor-not-allowed" : ""
        }`}
      >
        Login
      </button>
      <p className="text-red-500 mt-2 h-2">{error ?? ""}</p>
    </form>
  );
}
