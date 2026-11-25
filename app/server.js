// Load .env variables into process.env from the current working directory.
import "dotenv/config";

// Import Express for an HTTP server and Google's API client library.
import express from "express";
import * as google from "googleapis/build/src/apis/calendar/index.js";

const app = express();
const PORT = process.env.PORT;

// Create an OAuth2 client using your app's Google Cloud credentials.
// Creating this object does not make a network request.
const auth = new google.auth.OAuth2(
  process.env.GOOGLE_CLIENT_ID,     // Identifies your app
  process.env.GOOGLE_CLIENT_SECRET, // Authenticates your app
);

// Supply your account's refresh token so the library can obtain access tokens.
auth.setCredentials({
  refresh_token: process.env.GOOGLE_REFRESH_TOKEN,
});

// Create a Calendar API v3 client that uses the OAuth2 client for authentication.
const calendar = google.calendar({ version: "v3", auth });

async function getCalendarData() {
  try {
    // Request the earliest event that has not ended, including one in progress.
    // Wait for the response and extract its data property.
    const { data } = await calendar.events.list({
      calendarId: "primary", // Use the signed-in account's primary calendar.
      timeMin: new Date().toISOString(), // Only include events ending after now.
      singleEvents: true, // Expand recurring events into individual occurrences.
      orderBy: "startTime", // Sort by start time, earliest first.
      maxResults: 1, // Return at most one event.
    });

    const dataparse = data.items[0];

    const formatTime = (timestamp) =>
      new Date(timestamp).toLocaleTimeString("en-GB", {
        hour: "2-digit",
        minute: "2-digit",
      });


    const now = Date.now();
    const start = new Date(dataparse.start.dateTime).getTime();
    const end = new Date(dataparse.end.dateTime).getTime();

    const shortdata = {
        "title": dataparse.summary,
        "startsAt": start,
        "endsAt": end,
        "timeLabel": `${formatTime(start)} - ${formatTime(end)}`,
        "current": now >= start && now < end
    };

    return shortdata;

  } catch (error) {
    // Print the error and mark the process as unsuccessful when it finishes.
    console.error("Calendar request failed:", error.message);
    process.exitCode = 1;
  }
}

// console.log(typeof(await getCalendarData()));

app.get('/next', async (req, res) => {
  res.send(await getCalendarData());
});

app.use((req, res) => {
  res.status(404).send("404 - Page not found");
});

app.listen(PORT, "192.168.1.13", () => {
  console.log(`Server: http://192.168.1.13:${PORT}`);
});
