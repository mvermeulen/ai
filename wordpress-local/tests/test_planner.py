from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from wp_local_v2.planner import PlanError, load_plan
from wp_local_v2.utils import digest_json, utc_now_iso


class PlannerTests(unittest.TestCase):
    def test_load_plan_rejects_tampered_checksum(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            plan_file = Path(tmp) / "plan.json"
            payload = {
                "version": 1,
                "plan_id": "abc",
                "created_at": utc_now_iso(),
                "target_site": "https://example.com",
                "requires_ack": "APPROVE-abc",
                "max_operations": 10,
                "checksum": "wrong",
                "operations": [{"action": "create", "key": "post:x"}],
            }
            plan_file.write_text(json.dumps(payload), encoding="utf-8")

            with self.assertRaises(PlanError):
                load_plan(plan_file)

    def test_load_plan_accepts_valid_checksum(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            plan_file = Path(tmp) / "plan.json"
            operations = [{"action": "create", "key": "post:y"}]
            payload = {
                "version": 1,
                "plan_id": "abc",
                "created_at": utc_now_iso(),
                "target_site": "https://example.com",
                "requires_ack": "APPROVE-abc",
                "max_operations": 10,
                "checksum": digest_json(operations),
                "operations": operations,
            }
            plan_file.write_text(json.dumps(payload), encoding="utf-8")

            loaded = load_plan(plan_file)
            self.assertEqual(loaded["plan_id"], "abc")


if __name__ == "__main__":
    unittest.main()
