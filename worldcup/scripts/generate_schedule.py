#!/usr/bin/env python3
import csv
import json
import urllib.request
from datetime import date, datetime, timedelta
from pathlib import Path
from zoneinfo import ZoneInfo

groups = {
    "A": ["MEX", "RSA", "KOR", "CZE"],
    "B": ["CAN", "SUI", "QAT", "BIH"],
    "C": ["BRA", "MAR", "SCO", "HAI"],
    "D": ["USA", "PAR", "AUS", "TUR"],
    "E": ["GER", "ECU", "CIV", "CUW"],
    "F": ["NED", "JPN", "TUN", "SWE"],
    "G": ["BEL", "IRN", "EGY", "NZL"],
    "H": ["ESP", "URU", "KSA", "CPV"],
    "I": ["FRA", "SEN", "NOR", "IRQ"],
    "J": ["ARG", "AUT", "ALG", "JOR"],
    "K": ["POR", "COL", "UZB", "COD"],
    "L": ["ENG", "CRO", "PAN", "GHA"]
}

ESPN_SCOREBOARD_URL = "https://site.api.espn.com/apis/site/v2/sports/soccer/fifa.world/scoreboard"
# Yahoo/US scoreboards group fixtures by US local calendar day.
ESPN_SCOREBOARD_DAY_TZ = ZoneInfo("America/New_York")


def sync_group_stage_dates_from_espn(fixtures):
    """Overwrite group-stage dates using ESPN fixture dates when available.

    Dates are keyed by matchup regardless of home/away orientation.
    """
    pair_to_date = {}
    start = date(2026, 6, 11)
    end = date(2026, 6, 27)
    current = start

    while current <= end:
        date_str = current.strftime("%Y%m%d")
        url = f"{ESPN_SCOREBOARD_URL}?dates={date_str}"
        req = urllib.request.Request(
            url,
            headers={"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"},
        )
        with urllib.request.urlopen(req, timeout=20) as response:
            data = json.loads(response.read().decode("utf-8"))

        for event in data.get("events", []):
            raw_event_date = event.get("date", "")
            event_date = ""
            if raw_event_date:
                try:
                    # Convert UTC kickoff timestamp to local scoreboard day.
                    parsed_dt = datetime.fromisoformat(raw_event_date.replace("Z", "+00:00"))
                    event_date = parsed_dt.astimezone(ESPN_SCOREBOARD_DAY_TZ).date().isoformat()
                except Exception:
                    event_date = raw_event_date[:10]
            competitions = event.get("competitions", [])
            if not competitions:
                continue

            home = ""
            away = ""
            for competitor in competitions[0].get("competitors", []):
                abbr = competitor.get("team", {}).get("abbreviation", "").upper()
                if competitor.get("homeAway") == "home":
                    home = abbr
                elif competitor.get("homeAway") == "away":
                    away = abbr

            if home and away and event_date:
                key = "|".join(sorted([home, away]))
                pair_to_date[key] = event_date

        current += timedelta(days=1)

    updated = 0
    for fixture in fixtures:
        if fixture.get("stage") != "group":
            continue

        key = "|".join(sorted([fixture["home_team"].upper(), fixture["away_team"].upper()]))
        api_date = pair_to_date.get(key)
        if api_date and fixture["date"] != api_date:
            fixture["date"] = api_date
            updated += 1

    return updated, len(pair_to_date)

# Group stage dates (June 11 - June 27, 2026)
# Let's map each group's round to specific days
group_round_dates = {
    1: ["2026-06-11", "2026-06-12", "2026-06-13", "2026-06-14", "2026-06-15"],
    2: ["2026-06-16", "2026-06-17", "2026-06-18", "2026-06-19", "2026-06-20", "2026-06-21"],
    3: ["2026-06-22", "2026-06-23", "2026-06-24", "2026-06-25", "2026-06-26", "2026-06-27"]
}

# 104 matches schema: match_id,stage,group,date,home_team,away_team,home_score,away_score,home_penalty_score,away_penalty_score,status

fixtures = []
match_id = 1

# Generate 72 group stage matches (6 matches per group)
# Order: Round 1, Round 2, Round 3
for round_num in [1, 2, 3]:
    # Distribute matches across available dates for this round
    dates = group_round_dates[round_num]
    for group_idx, (g_name, g_teams) in enumerate(sorted(groups.items())):
        # Keep the two fixtures from the same group-round on the same date.
        # Cycle dates by group index to spread groups across the round window.
        date = dates[group_idx % len(dates)]
        if round_num == 1:
            # Team 0 vs Team 1, Team 2 vs Team 3
            pairings = [(g_teams[0], g_teams[1]), (g_teams[2], g_teams[3])]
        elif round_num == 2:
            # Team 0 vs Team 2, Team 1 vs Team 3
            pairings = [(g_teams[0], g_teams[2]), (g_teams[1], g_teams[3])]
        else:
            # Team 0 vs Team 3, Team 1 vs Team 2
            pairings = [(g_teams[0], g_teams[3]), (g_teams[1], g_teams[2])]

        for home, away in pairings:
            fixtures.append({
                "match_id": match_id,
                "stage": "group",
                "group": g_name,
                "date": date,
                "home_team": home,
                "away_team": away,
                "home_score": -1,
                "away_score": -1,
                "home_penalty_score": -1,
                "away_penalty_score": -1,
                "status": "scheduled"
            })
            match_id += 1

# Knockout Stages:
# Round of 32: Match 73 to 88 (16 matches, June 28 - July 3)
# Round of 16: Match 89 to 96 (8 matches, July 4 - July 7)
# Quarterfinals: Match 97 to 100 (4 matches, July 9 - July 11)
# Semifinals: Match 101 to 102 (2 matches, July 14 - July 15)
# Third Place: Match 103 (July 18)
# Final: Match 104 (July 19)

knockout_configs = [
    ("knockout_r32", 16, "2026-06-28", "2026-07-03"),
    ("knockout_r16", 8, "2026-07-04", "2026-07-07"),
    ("quarterfinals", 4, "2026-07-09", "2026-07-11"),
    ("semifinals", 2, "2026-07-14", "2026-07-15"),
    ("third_place", 1, "2026-07-18", "2026-07-18"),
    ("final", 1, "2026-07-19", "2026-07-19")
]

for stage_name, count, start_date, end_date in knockout_configs:
    # Simpler date mapping: just spread them
    for i in range(count):
        fixtures.append({
            "match_id": match_id,
            "stage": stage_name,
            "group": "N/A",
            "date": start_date if count == 1 else start_date,  # simplified
            "home_team": f"TBD_{match_id}",
            "away_team": f"TBD_{match_id}",
            "home_score": -1,
            "away_score": -1,
            "home_penalty_score": -1,
            "away_penalty_score": -1,
            "status": "scheduled"
        })
        match_id += 1

try:
    updated_count, mapped_count = sync_group_stage_dates_from_espn(fixtures)
    print(
        f"Synced group dates from ESPN for {updated_count} fixtures "
        f"(matchup dates available: {mapped_count})."
    )
except Exception as exc:
    print(f"Warning: ESPN date sync skipped ({exc}). Using fallback generated dates.")

# Write to file
output_dir = Path(__file__).parent.parent / "data"
output_dir.mkdir(parents=True, exist_ok=True)
output_file = output_dir / "schedule.csv"

fieldnames = ["match_id", "stage", "group", "date", "home_team", "away_team", "home_score", "away_score", "home_penalty_score", "away_penalty_score", "status"]
with open(output_file, "w", newline="", encoding="utf-8") as f:
    writer = csv.DictWriter(f, fieldnames=fieldnames)
    writer.writeheader()
    for fix in fixtures:
        writer.writerow(fix)

print(f"Generated {len(fixtures)} matches in {output_file}")
