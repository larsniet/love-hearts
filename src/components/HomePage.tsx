"use client";

import { useState, useEffect, useCallback } from "react";
import { FaHeart } from "react-icons/fa";

const baseUrl = process.env.NEXT_PUBLIC_BASE_URL ?? "http://localhost:3000";

interface HomePageProps {
  initialReachable: boolean;
  initialLedOn: boolean;
}

export default function HomePage({
  initialReachable,
  initialLedOn,
}: HomePageProps): JSX.Element {
  const [isReachable, setIsReachable] = useState(initialReachable);
  const [isLedOn, setIsLedOn] = useState(initialLedOn);

  const checkLedStatus = useCallback(async () => {
    if (!isReachable) return;

    try {
      const response = await fetch(`${baseUrl}/api/check-led-status`);
      if (response.ok) {
        const data = await response.json();
        setIsLedOn(data.status === "on");
      }
    } catch (error) {
      console.error("Error checking LED status:", error);
    }
  }, [isReachable, setIsLedOn]);

  const checkReachability = useCallback(async () => {
    try {
      const response = await fetch(`${baseUrl}/api/check-reachability`);
      const data = await response.json();
      if (data.reachable) {
        setIsReachable(true);
        checkLedStatus(); // Check LED status if reachable
      } else {
        setIsReachable(false);
      }
    } catch (error) {
      setIsReachable(false);
    }
  }, [setIsReachable, checkLedStatus]);

  const toggleLed = async () => {
    if (!isReachable) return;

    try {
      const response = await fetch(`${baseUrl}/api/toggle-led`);
      if (response.ok) {
        setIsLedOn((prev) => !prev);
      }
    } catch (error) {
      console.error("Error toggling LED:", error);
    }
  };

  useEffect(() => {
    const interval = setInterval(checkReachability, 5000); // Check reachability every 5 seconds
    return () => clearInterval(interval);
  }, [checkReachability]);

  return (
    <div className="mb-4">
      {isReachable ? (
        <button onClick={toggleLed} className="text-4xl">
          <FaHeart className={isLedOn ? "text-red-500" : "text-gray-500"} />
        </button>
      ) : (
        <span className="text-red-500 font-bold">Device Unreachable</span>
      )}
    </div>
  );
}
