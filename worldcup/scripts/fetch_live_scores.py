#!/usr/bin/env python3
"""Fetch live FIFA World Cup scores from ESPN scoreboard API and update schedule.csv in-place.
"""

from __future__ import annotations
import argparse
import csv
from datetime import date, timedelta
import json
import sys
import urllib.request
from pathlib import Path
from typing import Dict, List, Any

ESPN_SCOREBOARD_URL = "https://site.api.espn.com/apis/site/v2/sports/soccer/fifa.world/scoreboard"

def fetch_espn_scores(date_str: str | None = None) -> List[Dict[str, Any]]:
    """Fetch ESPN scoreboard events."""
    url = ESPN_SCOREBOARD_URL
    if date_str:
        url += f"?dates={date_str}"
    print(f"Fetching live soccer scores from: {url}")
    try:
        req = urllib.request.Request(
            url, 
            headers={"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"}
        )
        with urllib.request.urlopen(req) as response:
            data = json.loads(response.read().decode("utf-8"))
            return data.get("events", [])
    except Exception as e:
        print(f"Error fetching from ESPN: {e}", file=sys.stderr)
        return []

def fetch_default_window_scores() -> List[Dict[str, Any]]:
    """Fetch scores for yesterday and today to catch recently finished games."""
    events_by_id: Dict[str, Dict[str, Any]] = {}
    today = date.today()
    for day in (today - timedelta(days=1), today):
        day_events = fetch_espn_scores(day.strftime("%Y%m%d"))
        for event in day_events:
            event_id = event.get("id")
            if event_id:
                events_by_id[event_id] = event
    return list(events_by_id.values())

def parse_espn_games(events: List[Dict[str, Any]]) -> List[Dict[str, Any]]:
    """Parse ESPN events list into structured game dicts."""
    parsed_games = []
    for event in events:
        competitions = event.get("competitions", [])
        if not competitions:
            continue
        comp = competitions[0]

        status_type = comp.get("status", {}).get("type", {})
        status_name = status_type.get("name", "")

        # Map status to ours
        if status_name in {"STATUS_FINAL", "STATUS_FULL_TIME", "STATUS_AET", "STATUS_PENALTY_SHOOTOUT"}:
            status = "final"
        elif "IN_PROGRESS" in status_name or "HALFTIME" in status_name:
            status = "in_progress"
        else:
            status = "upcoming"

        # Determine home and away
        home_team = ""
        away_team = ""
        home_score = -1
        away_score = -1

        for competitor in comp.get("competitors", []):
            abbr = competitor.get("team", {}).get("abbreviation", "")
            score = -1
            try:
                score = int(competitor.get("score", -1))
            except (ValueError, TypeError):
                pass

            if competitor.get("homeAway") == "home":
                home_team = abbr
                home_score = score
            else:
                away_team = abbr
                away_score = score

        parsed_games.append({
            "home_team": home_team,
            "away_team": away_team,
            "home_score": home_score,
            "away_score": away_score,
            "status": status,
        })
    return parsed_games

def update_schedule_csv(schedule_path: Path, new_games: List[Dict[str, Any]]) -> int:
    """Merge new games into schedule.csv and write back in-place. Returns number of games updated."""
    if not schedule_path.exists():
        print(f"Error: schedule file not found at {schedule_path}", file=sys.stderr)
        return 0

    # Read existing schedule rows
    fieldnames = ["match_id", "stage", "group", "date", "home_team", "away_team", "home_score", "away_score", "home_penalty_score", "away_penalty_score", "status", "host_city"]
    rows = []
    with schedule_path.open(newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for r in reader:
            rows.append(r)

    updates_count = 0
    for ng in new_games:
        for idx, row in enumerate(rows):
            # Check for matches (either by home/away teams, ignoring case)
            if row["home_team"].upper() == ng["home_team"].upper() and row["away_team"].upper() == ng["away_team"].upper():
                changed = (
                    row["status"] != ng["status"] or
                    int(row["home_score"]) != ng["home_score"] or
                    int(row["away_score"]) != ng["away_score"]
                )
                if changed:
                    rows[idx]["status"] = ng["status"]
                    rows[idx]["home_score"] = str(ng["home_score"])
                    rows[idx]["away_score"] = str(ng["away_score"])
                    updates_count += 1

    # Write back to CSV
    with schedule_path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)

    return updates_count

def main() -> int:
    parser = argparse.ArgumentParser(description="Fetch and sync live World Cup scores from ESPN.")
    parser.add_argument(
        "--schedule", 
        default="data/schedule.csv", 
        type=Path, 
        help="Path to the schedule.csv file to update (default: data/schedule.csv)"
    )
    parser.add_argument(
        "--date",
        type=str,
        help="Date to fetch scores for in YYYYMMDD or YYYY-MM-DD format"
    )

    args = parser.parse_args()
    schedule_path: Path = args.schedule
    
    if args.date:
        date_arg = args.date.replace("-", "")
        events = fetch_espn_scores(date_arg)
    else:
        events = fetch_default_window_scores()
    parsed = parse_espn_games(events)
    if not parsed:
        print("No games found on scoreboard or error occurred.")
        return 1

    updates = update_schedule_csv(schedule_path, parsed)
    print(f"Processed {len(parsed)} matches, updated {updates} in schedule.csv.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
