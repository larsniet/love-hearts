"use client";

import { useState, useEffect } from "react";
import { FaHeart } from "react-icons/fa";

export default function HomePage(): JSX.Element {
  const [isLedOn, setIsLedOn] = useState<boolean>(false);
  const [isLoading, setIsLoading] = useState<boolean>(false);

  const toggleLed = async () => {
    setIsLoading(true);
    setIsLedOn((prev) => !prev);
    const action = isLedOn ? "OFF" : "ON";
    try {
      const response = await fetch(`/api/toggle-led`, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
        },
        body: JSON.stringify({ action }),
      });
      if (response.ok) {
        // State will be updated via polling
      } else {
        console.error("Failed to toggle LED");
      }
    } catch (error) {
      console.error("Error toggling LED:", error);
    } finally {
      setIsLoading(false);
    }
  };

  // Poll for LED status
  useEffect(() => {
    const fetchLedStatus = async () => {
      try {
        const response = await fetch(`/api/check-led-status`);
        if (response.ok) {
          const data = await response.json();
          const serverLedStatus = data.status === "ON";
          setIsLedOn(serverLedStatus);
        } else {
          console.error("Failed to check LED status");
        }
      } catch (error) {
        console.error("Error checking LED status:", error);
      }
    };

    // Fetch immediately on mount
    fetchLedStatus();

    // Set up interval to poll every 2 seconds
    const interval = setInterval(fetchLedStatus, 2000);

    // Clean up on unmount
    return () => clearInterval(interval);
  }, []);

  return (
    <div className="mb-4">
      <button onClick={toggleLed} disabled={isLoading} className="text-4xl">
        <FaHeart className={isLedOn ? "text-red-500" : "text-gray-500"} />
      </button>
    </div>
  );
}
