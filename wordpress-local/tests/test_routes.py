from pathlib import Path

import pytest

from wpsync import routes

GPX_TEMPLATE = """<?xml version="1.0" encoding="UTF-8"?>
<gpx version="1.1" creator="test" xmlns="http://www.topografix.com/GPX/1/1">
  <trk><name>Test Track</name><trkseg>
{points}
  </trkseg></trk>
</gpx>
"""


def make_gpx(tmp_path: Path, points, name="track.gpx") -> Path:
    lines = []
    for lat, lon, ele, time in points:
        ele_xml = f"<ele>{ele}</ele>" if ele is not None else ""
        time_xml = f"<time>{time}</time>" if time is not None else ""
        lines.append(f'    <trkpt lat="{lat}" lon="{lon}">{ele_xml}{time_xml}</trkpt>')
    gpx = GPX_TEMPLATE.format(points="\n".join(lines))
    path = tmp_path / name
    path.write_text(gpx, encoding="utf-8")
    return path


def test_parse_gpx_basic(tmp_path: Path):
    path = make_gpx(
        tmp_path,
        [
            (30.0, -97.0, 100.0, "2026-06-01T09:00:00Z"),
            (30.01, -97.0, 110.0, "2026-06-01T09:10:00Z"),
            (30.02, -97.0, 105.0, "2026-06-01T09:20:00Z"),
        ],
    )
    stats = routes.parse_gpx(path)
    assert len(stats.points) == 3
    assert stats.distance_km > 0
    assert stats.start_time == "2026-06-01T09:00:00Z"
    assert stats.end_time == "2026-06-01T09:20:00Z"
    assert stats.min_ele_m == 100.0
    assert stats.max_ele_m == 110.0


def test_parse_gpx_distance_matches_known_value(tmp_path: Path):
    # Two points exactly 1 degree of latitude apart on the equator: ~111.19 km.
    path = make_gpx(tmp_path, [(0.0, 0.0, None, None), (1.0, 0.0, None, None)])
    stats = routes.parse_gpx(path)
    assert stats.distance_km == pytest.approx(111.19, abs=0.5)


def test_parse_gpx_elevation_gain_and_loss(tmp_path: Path):
    # Simple up-down-up profile with no smoothing noise (window > point count keeps raw deltas).
    path = make_gpx(
        tmp_path,
        [
            (0.0, 0.0, 100.0, None),
            (0.0, 0.001, 200.0, None),
            (0.0, 0.002, 150.0, None),
            (0.0, 0.003, 250.0, None),
        ],
    )
    stats = routes.parse_gpx(path)
    assert stats.elevation_gain_m > 0
    assert stats.elevation_loss_m > 0


def test_parse_gpx_missing_file_raises(tmp_path: Path):
    with pytest.raises(routes.GPXParseError):
        routes.parse_gpx(tmp_path / "does-not-exist.gpx")


def test_parse_gpx_empty_track_raises(tmp_path: Path):
    path = tmp_path / "empty.gpx"
    path.write_text(
        '<?xml version="1.0"?><gpx version="1.1" xmlns="http://www.topografix.com/GPX/1/1"><trk><trkseg></trkseg></trk></gpx>',
        encoding="utf-8",
    )
    with pytest.raises(routes.GPXParseError):
        routes.parse_gpx(path)


def test_parse_gpx_invalid_xml_raises(tmp_path: Path):
    path = tmp_path / "broken.gpx"
    path.write_text("not xml at all <<<", encoding="utf-8")
    with pytest.raises(routes.GPXParseError):
        routes.parse_gpx(path)


def test_parse_gpx_without_namespace_still_works(tmp_path: Path):
    path = tmp_path / "no-ns.gpx"
    path.write_text(
        '<?xml version="1.0"?><gpx version="1.0"><trk><trkseg>'
        '<trkpt lat="30.0" lon="-97.0"><ele>100</ele></trkpt>'
        '<trkpt lat="30.01" lon="-97.0"><ele>110</ele></trkpt>'
        "</trkseg></trk></gpx>",
        encoding="utf-8",
    )
    stats = routes.parse_gpx(path)
    assert len(stats.points) == 2


def test_svg_elevation_profile_basic():
    svg = routes.svg_elevation_profile([100.0, 150.0, 120.0, 200.0])
    assert svg.startswith("<svg")
    assert "polyline" in svg


def test_svg_elevation_profile_needs_at_least_two_points():
    assert routes.svg_elevation_profile([100.0]) == ""
    assert routes.svg_elevation_profile([]) == ""


def test_svg_elevation_profile_downsamples_large_series():
    elevations = [100.0 + (i % 10) for i in range(5000)]
    svg = routes.svg_elevation_profile(elevations)
    # Should not embed one point per input sample.
    assert svg.count(",") < 1000


def test_render_route_html_includes_stats(tmp_path: Path):
    path = make_gpx(
        tmp_path,
        [
            (30.0, -97.0, 100.0, "2026-06-01T09:00:00Z"),
            (30.05, -97.0, 200.0, "2026-06-01T10:00:00Z"),
        ],
    )
    html_out = routes.render_route_html(path, "media/routes/day1.gpx")
    assert "Distance" in html_out
    assert "Elevation gain" in html_out
    assert 'data-wpsync-route="media/routes/day1.gpx"' in html_out
    assert "<svg" in html_out


def test_render_route_html_handles_missing_file_gracefully(tmp_path: Path):
    html_out = routes.render_route_html(tmp_path / "missing.gpx", "media/routes/missing.gpx")
    assert "could not render route" in html_out
    assert 'data-wpsync-route="media/routes/missing.gpx"' in html_out
    # Must not raise -- caller relies on this never throwing.


def test_render_route_html_without_elevation_data(tmp_path: Path):
    path = make_gpx(tmp_path, [(30.0, -97.0, None, None), (30.01, -97.0, None, None)])
    html_out = routes.render_route_html(path, "media/routes/flat.gpx")
    assert "Distance" in html_out
    assert "Elevation gain" not in html_out
    assert "<svg" not in html_out
