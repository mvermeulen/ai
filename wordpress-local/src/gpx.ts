/**
 * GPX route file parsing -- a capability v1 doesn't have. Strava links are
 * great once a ride is uploaded, but on a multi-week tour that doesn't
 * always happen same-day. This lets a day's post embed distance/elevation
 * stats straight from the .gpx a bike computer or phone app exported,
 * entirely offline, before the ride ever reaches Strava.
 */

export interface TrackPoint {
  lat: number;
  lon: number;
  ele: number | null;
  time: string | null;
}

export interface RouteSummary {
  points: number;
  distanceKm: number;
  elevationGainM: number;
  movingTimeMinutes: number | null;
}

const TRKPT_RE = /<trkpt\b[^>]*\blat="(-?[\d.]+)"[^>]*\blon="(-?[\d.]+)"[^>]*>([\s\S]*?)<\/trkpt>/g;
const ELE_RE = /<ele>(-?[\d.]+)<\/ele>/;
const TIME_RE = /<time>([^<]+)<\/time>/;

export function parseGpx(xml: string): TrackPoint[] {
  const points: TrackPoint[] = [];
  for (const m of xml.matchAll(TRKPT_RE)) {
    const inner = m[3];
    const eleMatch = ELE_RE.exec(inner);
    const timeMatch = TIME_RE.exec(inner);
    points.push({
      lat: parseFloat(m[1]),
      lon: parseFloat(m[2]),
      ele: eleMatch ? parseFloat(eleMatch[1]) : null,
      time: timeMatch ? timeMatch[1] : null,
    });
  }
  return points;
}

function toRad(deg: number): number {
  return (deg * Math.PI) / 180;
}

function haversineKm(a: TrackPoint, b: TrackPoint): number {
  const R = 6371;
  const dLat = toRad(b.lat - a.lat);
  const dLon = toRad(b.lon - a.lon);
  const lat1 = toRad(a.lat);
  const lat2 = toRad(b.lat);
  const h = Math.sin(dLat / 2) ** 2 + Math.cos(lat1) * Math.cos(lat2) * Math.sin(dLon / 2) ** 2;
  return 2 * R * Math.asin(Math.sqrt(h));
}

export function summarizeRoute(points: TrackPoint[]): RouteSummary {
  let distanceKm = 0;
  let elevationGainM = 0;
  for (let i = 1; i < points.length; i++) {
    distanceKm += haversineKm(points[i - 1], points[i]);
    const prevEle = points[i - 1].ele;
    const ele = points[i].ele;
    if (prevEle != null && ele != null && ele > prevEle) {
      elevationGainM += ele - prevEle;
    }
  }
  let movingTimeMinutes: number | null = null;
  const first = points[0];
  const last = points[points.length - 1];
  if (first?.time && last?.time) {
    const ms = new Date(last.time).getTime() - new Date(first.time).getTime();
    if (Number.isFinite(ms) && ms > 0) movingTimeMinutes = Math.round(ms / 60000);
  }
  return {
    points: points.length,
    distanceKm: Math.round(distanceKm * 10) / 10,
    elevationGainM: Math.round(elevationGainM),
    movingTimeMinutes,
  };
}

function escapeHtml(s: string): string {
  return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
}

/** Renders a route summary as a Gutenberg group block, ready to paste into a post's content. */
export function renderRouteSummaryHtml(summary: RouteSummary, title = "Today's route"): string {
  const bits = [`${summary.distanceKm} km`, `${summary.elevationGainM} m climbing`];
  if (summary.movingTimeMinutes != null) {
    const h = Math.floor(summary.movingTimeMinutes / 60);
    const m = summary.movingTimeMinutes % 60;
    bits.push(h > 0 ? `${h}h ${m}m elapsed` : `${m}m elapsed`);
  }
  return [
    '<!-- wp:group {"className":"wpsync-route-summary"} -->',
    '<div class="wp-block-group wpsync-route-summary">',
    '<!-- wp:heading {"level":4} -->',
    `<h4 class="wp-block-heading">${escapeHtml(title)}</h4>`,
    "<!-- /wp:heading -->",
    "<!-- wp:paragraph -->",
    `<p class="wp-block-paragraph">${bits.map(escapeHtml).join(" &middot; ")}</p>`,
    "<!-- /wp:paragraph -->",
    "</div>",
    "<!-- /wp:group -->",
  ].join("\n");
}
