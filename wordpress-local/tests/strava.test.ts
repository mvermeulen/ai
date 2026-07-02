import { describe, expect, it } from "vitest";
import { collectStravaLinks } from "../src/lib/strava.js";

describe("collectStravaLinks", () => {
  it("extracts unique Strava route links", () => {
    const markdown = [
      "Morning ride: https://www.strava.com/routes/1234",
      "Repeat: https://www.strava.com/routes/1234",
      "Another: https://www.strava.com/routes/5678"
    ].join("\n");

    expect(collectStravaLinks(markdown)).toEqual([
      "https://www.strava.com/routes/1234",
      "https://www.strava.com/routes/5678"
    ]);
  });
});
