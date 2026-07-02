const STRAVA_ROUTE_REGEX = /https?:\/\/www\.strava\.com\/routes\/\d+/g;

export function collectStravaLinks(markdown: string): string[] {
  const matches = markdown.match(STRAVA_ROUTE_REGEX) ?? [];
  return [...new Set(matches)];
}
