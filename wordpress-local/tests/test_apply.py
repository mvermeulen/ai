from __future__ import annotations

import unittest

from wp_local_v2.apply import ApplyOptions, apply_plan
from wp_local_v2.config import AppConfig
from wp_local_v2.planner import PlanError
from wp_local_v2.utils import utc_now_iso


class ApplyTests(unittest.TestCase):
    def test_apply_plan_requires_exact_ack_phrase(self) -> None:
        plan = {
            "version": 1,
            "plan_id": "plan-1",
            "created_at": utc_now_iso(),
            "target_site": "https://example.com",
            "requires_ack": "APPROVE-plan-1",
            "max_operations": 5,
            "checksum": "unused-in-apply",
            "operations": [],
        }

        cfg = AppConfig(
            site_url="https://example.com",
            username=None,
            app_password=None,
            allowed_hosts=["example.com"],
            max_plan_operations=20,
            max_plan_age_hours=72,
        )

        with self.assertRaises(PlanError):
            apply_plan(
                plan,
                cfg,
                ApplyOptions(
                    execute=False,
                    ack="WRONG",
                    expected_host=None,
                    force_large=False,
                    force_conflicts=False,
                ),
            )


if __name__ == "__main__":
    unittest.main()
