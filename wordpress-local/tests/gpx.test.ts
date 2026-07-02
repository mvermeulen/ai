import assert from "node:assert/strict";
import { test } from "node:test";
import { parseGpx, renderRouteSummaryHtml, summarizeRoute } from "../src/gpx.js";

const SAMPLE_GPX = `<?xml version="1.0"?>
<gpx version="1.1">
<trk><name>Test ride</name><trkseg>
<trkpt lat="35.0844" lon="-106.6504"><ele>1620</ele><time>2026-07-01T08:00:00Z</time></trkpt>
<trkpt lat="35.1044" lon="-106.6304"><ele>1650</ele><time>2026-07-01T08:30:00Z</time></trkpt>
<trkpt lat="35.1244" lon="-106.6104"><ele>1700</ele><time>2026-07-01T09:15:00Z</time></trkpt>
<trkpt lat="35.1444" lon="-106.5904"><ele>1680</ele><time>2026-07-01T10:00:00Z</time></trkpt>
</trkseg></trk>
</gpx>`;

test("parseGpx extracts track points with elevation and time", () => {
  const points = parseGpx(SAMPLE_GPX);
  assert.equal(points.length, 4);
  assert.equal(points[0].lat, 35.0844);
  assert.equal(points[0].lon, -106.6504);
  assert.equal(points[0].ele, 1620);
  assert.equal(points[0].time, "2026-07-01T08:00:00Z");
});

test("summarizeRoute computes distance, elevation gain, and elapsed time", () => {
  const summary = summarizeRoute(parseGpx(SAMPLE_GPX));
  assert.equal(summary.points, 4);
  assert.ok(summary.distanceKm > 0, "distance should be positive");
  // elevation only climbs in this fixture: 1620->1650->1700->1680 => gains of 30+50=80, no descent counted
  assert.equal(summary.elevationGainM, 80);
  assert.equal(summary.movingTimeMinutes, 120);
});

test("summarizeRoute handles a single point without crashing", () => {
  const summary = summarizeRoute(parseGpx('<gpx><trkpt lat="1" lon="2"></trkpt></gpx>'));
  assert.equal(summary.points, 1);
  assert.equal(summary.distanceKm, 0);
  assert.equal(summary.elevationGainM, 0);
});

test("renderRouteSummaryHtml renders a pasteable Gutenberg group block", () => {
  const html = renderRouteSummaryHtml({ points: 4, distanceKm: 8.6, elevationGainM: 80, movingTimeMinutes: 120 }, "Day 3");
  assert.match(html, /wp:group/);
  assert.match(html, /Day 3/);
  assert.match(html, /8\.6 km/);
  assert.match(html, /80 m climbing/);
  assert.match(html, /2h 0m elapsed/);
});
