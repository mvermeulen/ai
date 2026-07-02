from __future__ import annotations

import argparse
import json
from pathlib import Path

from .apply import ApplyOptions, apply_plan
from .config import ConfigError, load_config
from .paths import ensure_layout
from .planner import build_plan, load_plan, summarize_plan
from .snapshot import pull_snapshot
from .vault import create_entry, load_entries


def main() -> None:
    parser = argparse.ArgumentParser(prog="wp-local-plan", description="Offline WordPress sync via immutable plans")
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("init", help="Create local vault and state folders")

    new_cmd = sub.add_parser("new", help="Create a new vault entry")
    new_cmd.add_argument("--type", choices=["post", "page"], required=True)
    new_cmd.add_argument("--title", required=True)

    snapshot_cmd = sub.add_parser("snapshot", help="Pull remote index snapshot")
    snapshot_cmd.add_argument("--limit", type=int, default=40)

    build_cmd = sub.add_parser("plan-build", help="Build immutable plan from local changes")
    build_cmd.add_argument("--name", help="Optional custom plan ID")

    show_cmd = sub.add_parser("plan-show", help="Show plan summary")
    show_cmd.add_argument("plan_file")

    apply_cmd = sub.add_parser("plan-apply", help="Apply a plan with explicit approval")
    apply_cmd.add_argument("plan_file")
    apply_cmd.add_argument("--ack", required=True, help="Exact ACK phrase from plan")
    apply_cmd.add_argument("--execute", action="store_true", help="Execute remote writes (default dry-run)")
    apply_cmd.add_argument("--confirm-host", help="Require exact host name")
    apply_cmd.add_argument("--force-large", action="store_true", help="Allow operations beyond max")
    apply_cmd.add_argument("--force-conflicts", action="store_true", help="Override remote-modified conflicts")

    args = parser.parse_args()

    try:
        ensure_layout()
        if args.command == "init":
            print("Initialized vault and state folders")
            return

        if args.command == "new":
            entry = create_entry(args.type, args.title)
            print(f"Created {entry}")
            return

        config = load_config()

        if args.command == "snapshot":
            items = pull_snapshot(config, limit_per_type=args.limit)
            print(f"Snapshot saved with {len(items)} entries")
            return

        if args.command == "plan-build":
            entries = load_entries()
            plan_file = build_plan(entries, config, plan_name=args.name)
            plan = load_plan(plan_file)
            print(summarize_plan(plan))
            print(f"Plan file: {plan_file}")
            return

        if args.command == "plan-show":
            plan = load_plan(Path(args.plan_file))
            print(summarize_plan(plan))
            print("Operations:")
            for op in plan.get("operations", []):
                print(f"- {op['action']} {op['entry_type']} {op['slug']}")
            return

        if args.command == "plan-apply":
            plan = load_plan(Path(args.plan_file))
            result = apply_plan(
                plan,
                config,
                ApplyOptions(
                    execute=bool(args.execute),
                    ack=args.ack,
                    expected_host=args.confirm_host,
                    force_large=bool(args.force_large),
                    force_conflicts=bool(args.force_conflicts),
                ),
            )
            print(json.dumps(result, indent=2))
            if not args.execute:
                print("Dry-run complete. Add --execute to perform writes.")
            return

    except (ConfigError, RuntimeError) as exc:
        print(f"Error: {exc}")
        raise SystemExit(1) from exc


if __name__ == "__main__":
    main()
