from __future__ import annotations

import unittest

from wp_local_v2.utils import collect_strava_links, replace_asset_tokens, slugify_title


class UtilsTests(unittest.TestCase):
    def test_slugify_title(self) -> None:
        self.assertEqual(slugify_title("Great Divide Day 5"), "great-divide-day-5")

    def test_collect_strava_links_unique(self) -> None:
        markdown = "\n".join(
            [
                "https://www.strava.com/routes/123",
                "https://www.strava.com/routes/123",
                "https://www.strava.com/routes/999",
            ]
        )
        self.assertEqual(
            collect_strava_links(markdown),
            ["https://www.strava.com/routes/123", "https://www.strava.com/routes/999"],
        )

    def test_replace_asset_tokens(self) -> None:
        body = "Photo: asset://camp.jpg and asset://map.png"
        updated = replace_asset_tokens(
            body,
            {
                "camp.jpg": "https://example.com/camp.jpg",
                "map.png": "https://example.com/map.png",
            },
        )
        self.assertIn("https://example.com/camp.jpg", updated)
        self.assertIn("https://example.com/map.png", updated)


if __name__ == "__main__":
    unittest.main()
