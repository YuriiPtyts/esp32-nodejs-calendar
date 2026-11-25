import "dotenv/config";
import http from "node:http";
import { randomBytes } from "node:crypto";
import { google } from "googleapis";

const redirectUri = "http://127.0.0.1:3000/callback";

const auth = new google.auth.OAuth2(
  process.env.GOOGLE_CLIENT_ID,
  process.env.GOOGLE_CLIENT_SECRET,
  redirectUri,
);

const state = randomBytes(32).toString("hex");

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, redirectUri);

  if (url.pathname !== "/callback") {
    res.writeHead(404).end();
    return;
  }

  if (url.searchParams.get("state") !== state) {
    res.writeHead(400).end("Invalid authorization state.");
    return;
  }

  try {
    const code = url.searchParams.get("code");

    if (!code) {
      throw new Error(url.searchParams.get("error") ?? "Missing code");
    }

    const { tokens } = await auth.getToken(code);

    if (!tokens.refresh_token) {
      throw new Error("Google did not return a refresh token.");
    }

    console.log("\nAdd this line to your .env:\n");
    console.log(`GOOGLE_REFRESH_TOKEN=${tokens.refresh_token}`);

    res.end("Authorized! You can close this tab.");
  } catch (error) {
    console.error(error.message);
    res.writeHead(400).end("Authorization failed. Check your terminal.");
  } finally {
    server.close();
  }
});

server.listen(3000, "127.0.0.1", () => {
  const url = auth.generateAuthUrl({
    access_type: "offline",
    prompt: "consent",
    scope: ["https://www.googleapis.com/auth/calendar.events.readonly"],
    state,
  });

  console.log("Open this URL in your browser:\n");
  console.log(url);
});