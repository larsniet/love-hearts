// app/api/login/route.ts
import { NextRequest, NextResponse } from "next/server";

export async function POST(request: NextRequest) {
  const { password } = await request.json();

  if (password === process.env.AUTH_PASSWORD) {
    const response = NextResponse.json({ success: true });

    // Set the auth cookie
    response.cookies.set("auth", password, {
      httpOnly: true,
      secure: process.env.NODE_ENV === "production",
      maxAge: 60 * 60 * 24 * 7, // 7 days
      path: "/",
    });

    return response;
  } else {
    return NextResponse.json({ success: false }, { status: 401 });
  }
}
