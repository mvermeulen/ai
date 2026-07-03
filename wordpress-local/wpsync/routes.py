"""GPX route parsing: turn a `.gpx` track log dropped into ``content/media/``
into a distance/elevation summary block and an inline SVG elevation
profile, computed entirely offline with no external mapping service.

Referenced from a post/page body with a ``{{route:media/routes/day12.gpx}}``
marker (path relative to the content directory, same convention as image
references). This is a companion to the existing ``{{strava:...}}`` marker,
not a replacement -- use ``{{strava:...}}`` for a route already published on
Strava, and ``{{route:...}}`` for a GPX file you're carrying around that
hasn't been (or never will be) uploaded there.

Uses only the standard library (``xml.etree.ElementTree``) so this feature
adds no new dependency.
"""
from __future__ import annotations

import html
import math
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import List, Optional
from xml.etree import ElementTree as ET

EARTH_RADIUS_KM = 6371.0088
ELEVATION_SMOOTHING_WINDOW = 5
MAX_SVG_POINTS = 200


class GPXParseError(ValueError):
    pass


@dataclass
class TrackPoint:
    lat: float
    lon: float
    ele: Optional[float] = None
    time: Optional[str] = None


@dataclass
class RouteStats:
    points: List[TrackPoint] = field(default_factory=list)
    distance_km: float = 0.0
    elevation_gain_m: float = 0.0
    elevation_loss_m: float = 0.0
    min_ele_m: Optional[float] = None
    max_ele_m: Optional[float] = None
    start_time: Optional[str] = None
    end_time: Optional[str] = None

    @property
    def distance_mi(self) -> float:
        return self.distance_km * 0.621371

    @property
    def has_elevation(self) -> bool:
        return any(p.ele is not None for p in self.points)


def _haversine_km(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    phi1, phi2 = math.radians(lat1), math.radians(lat2)
    dphi = math.radians(lat2 - lat1)
    dlambda = math.radians(lon2 - lon1)
    a = math.sin(dphi / 2) ** 2 + math.cos(phi1) * math.cos(phi2) * math.sin(dlambda / 2) ** 2
    return 2 * EARTH_RADIUS_KM * math.asin(min(1.0, math.sqrt(a)))


def _smooth(values: List[float], window: int = ELEVATION_SMOOTHING_WINDOW) -> List[float]:
    if window <= 1 or len(values) <= window:
        return values
    half = window // 2
    out = []
    for i in range(len(values)):
        lo, hi = max(0, i - half), min(len(values), i + half + 1)
        out.append(sum(values[lo:hi]) / (hi - lo))
    return out


def parse_gpx(path: Path) -> RouteStats:
    if not path.exists():
        raise GPXParseError(f"GPX file not found: {path}")
    try:
        tree = ET.parse(path)
    except ET.ParseError as exc:
        raise GPXParseError(f"Could not parse {path.name} as GPX/XML: {exc}") from exc

    root = tree.getroot()
    points: List[TrackPoint] = []
    for trkpt in root.findall(".//{*}trkpt"):
        lat_raw, lon_raw = trkpt.get("lat"), trkpt.get("lon")
        if lat_raw is None or lon_raw is None:
            continue
        ele_el = trkpt.find("{*}ele")
        time_el = trkpt.find("{*}time")
        try:
            lat, lon = float(lat_raw), float(lon_raw)
        except ValueError:
            continue
        ele = None
        if ele_el is not None and ele_el.text:
            try:
                ele = float(ele_el.text)
            except ValueError:
                ele = None
        points.append(TrackPoint(lat=lat, lon=lon, ele=ele, time=time_el.text if time_el is not None else None))

    if not points:
        raise GPXParseError(f"No track points found in {path.name}")

    stats = RouteStats(points=points)

    distance = 0.0
    for a, b in zip(points, points[1:]):
        distance += _haversine_km(a.lat, a.lon, b.lat, b.lon)
    stats.distance_km = distance

    elevations = [p.ele for p in points if p.ele is not None]
    if elevations:
        stats.min_ele_m = min(elevations)
        stats.max_ele_m = max(elevations)
        smoothed = _smooth(elevations)
        gain = loss = 0.0
        for a, b in zip(smoothed, smoothed[1:]):
            delta = b - a
            if delta > 0:
                gain += delta
            else:
                loss += -delta
        stats.elevation_gain_m = gain
        stats.elevation_loss_m = loss

    times = [p.time for p in points if p.time]
    if times:
        stats.start_time = times[0]
        stats.end_time = times[-1]

    return stats


def _downsample(values: List[float], max_points: int = MAX_SVG_POINTS) -> List[float]:
    if len(values) <= max_points:
        return values
    stride = len(values) / max_points
    return [values[int(i * stride)] for i in range(max_points)]


def svg_elevation_profile(elevations: List[float], width: int = 600, height: int = 140) -> str:
    if len(elevations) < 2:
        return ""
    values = _downsample(elevations)
    lo, hi = min(values), max(values)
    span = (hi - lo) or 1.0
    pad = 8
    plot_w, plot_h = width - 2 * pad, height - 2 * pad
    coords = []
    for i, v in enumerate(values):
        x = pad + (i / (len(values) - 1)) * plot_w
        y = pad + (1 - (v - lo) / span) * plot_h
        coords.append(f"{x:.1f},{y:.1f}")
    polyline = " ".join(coords)
    fill_path = f"M{pad},{height - pad} L" + " L".join(coords) + f" L{width - pad},{height - pad} Z"
    return (
        f'<svg viewBox="0 0 {width} {height}" xmlns="http://www.w3.org/2000/svg" '
        f'role="img" aria-label="Elevation profile" class="wpsync-elevation-profile">'
        f'<path d="{fill_path}" fill="#dce9f7" stroke="none"/>'
        f'<polyline points="{polyline}" fill="none" stroke="#2c3338" stroke-width="1.5"/>'
        f"</svg>"
    )


def _fmt(value: float, unit: str) -> str:
    return f"{value:,.1f} {unit}"


def render_route_html(gpx_path: Path, marker_path: str) -> str:
    """Render a `{{route:...}}` marker to a Gutenberg-compatible HTML block.

    Never raises: a missing/unparseable GPX file becomes a clearly-labeled
    placeholder block instead of aborting the whole render, since this is
    called during offline `status`/`diff`/`preview` where crashing the
    entire command over one broken reference would be a worse outcome than
    a visible warning.
    """
    safe_path = html.escape(marker_path)
    try:
        stats = parse_gpx(gpx_path)
    except GPXParseError as exc:
        body = (
            f'<p class="wp-block-paragraph wpsync-route-error">'
            f"&#9888; wpsync could not render route <code>{safe_path}</code>: {html.escape(str(exc))}</p>"
        )
        return _wrap(marker_path, body)

    stat_lines = [f"<strong>Distance:</strong> {_fmt(stats.distance_km, 'km')} ({_fmt(stats.distance_mi, 'mi')})"]
    if stats.has_elevation:
        stat_lines.append(
            f"<strong>Elevation gain:</strong> {_fmt(stats.elevation_gain_m, 'm')} &middot; "
            f"<strong>loss:</strong> {_fmt(stats.elevation_loss_m, 'm')}"
        )
    if stats.start_time:
        stat_lines.append(f"<strong>Recorded:</strong> {html.escape(stats.start_time)}")

    stats_html = "<br/>".join(stat_lines)
    svg = ""
    if stats.has_elevation:
        elevations = [p.ele for p in stats.points if p.ele is not None]
        svg = svg_elevation_profile(elevations)

    body = f'<p class="wp-block-paragraph">{stats_html}</p>'
    if svg:
        body += f"\n{svg}"
    return _wrap(marker_path, body)


def _wrap(marker_path: str, inner_html: str) -> str:
    safe_path = html.escape(marker_path, quote=True)
    return (
        f'<!-- wp:group -->\n'
        f'<div class="wp-block-group wpsync-route" data-wpsync-route="{safe_path}">\n'
        f"{inner_html}\n"
        f"</div>\n"
        f"<!-- /wp:group -->"
    )
