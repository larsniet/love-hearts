// middleware.ts
import { NextRequest, NextResponse } from "next/server";

export function middleware(request: NextRequest) {
  const authCookie = request.cookies.get("auth");
  const isAuthenticated = authCookie?.value === process.env.AUTH_PASSWORD;

  const { pathname } = request.nextUrl;

  // Allow access to the login page and the login API
  if (pathname.startsWith("/login") || pathname.startsWith("/api/login")) {
    return NextResponse.next();
  }

  // If the user is not authenticated, redirect to the login page
  if (!isAuthenticated) {
    return NextResponse.redirect(new URL("/login", request.url));
  }

  // If the user is authenticated, continue to the requested page
  return NextResponse.next();
}

export const config = {
  matcher: ["/", "/((?!api|_next/static|favicon.ico).*)"],
};
