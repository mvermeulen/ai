# World Cup Game Tracker & Simulator: Investigation Report

This document reviews the C++ NFL tracking and simulation prototype (`nfl3`) and outlines the findings, architectural comparisons, and open questions required to build a similar application for tracking and simulating FIFA World Cup games.

---

## 1. Analysis of the NFL Prototype (`nfl3`)

The `nfl3` application is a C++ command-line tool with an embedded web server that implements a complete end-to-end pipeline for regular season tracking and playoff forecasting.

### 1.1 Data Layer & Ingestion
*   **Static Data (`teams.csv`)**: Holds team details (abbreviation, full name, conference, division).
*   **Dynamic Data (`schedule.csv`)**: Records the 18-week regular-season games, dates, scores, and status (`upcoming`/`final`).
*   **Historical Data (`data/historical/<year>.csv`)**: Historical regular-season results (1999–present) sourced from `nflverse` for backfitting and validation.
*   **Live score syncing**: A Python script (`scripts/fetch_live_scores.py`) fetches live game states from ESPN's Scoreboard API and updates `schedule.csv` in-place.

### 1.2 Model & Tiebreaker Engine
*   **Data Structures**: 
    *   `Team`: Tracks win/loss/tie records, points for/against, and divisional/conference records.
    *   `Game`: Represents home/away teams, scores, and status.
    *   `Season`: Manages the collection of teams and games and computes standings.
*   **Tiebreaker Rules (`Tiebreaker.cpp`)**: Implements NFL division and wildcard tiebreaker chains (Head-to-Head, Division Record, Common Games, Conference Record, Strength of Victory, Strength of Schedule). Uses an iterative elimination-and-restart strategy.

### 1.3 Simulation Engine (`MonteCarlo.cpp`)
*   **Win Probability Model**: Employs a logistic regression model based on home-field advantage and relative team strength (using current win percentage blended with prior-season win percentages).
*   **Simulation Loop**: Runs 100k Monte Carlo iterations. In each iteration, it:
    1.  Simulates remaining regular-season games using estimated win probabilities.
    2.  Computes final standings and applies tiebreaker rules to seed teams.
    3.  Simulates the single-elimination post-season bracket (Wild Card, Divisional, Conference Championships, Super Bowl) and tracks occurrences of playoff advances.
*   **Concurrency**: Accelerated using OpenMP (`#pragma omp parallel for`) when compiled with multi-threading support.

### 1.4 Web & CLI Interface
*   **CLI**: Commands to inspect standings (`status`), simulate remaining weeks (`simulate`), compute playoff deltas for upcoming games (`impact`), fit model coefficients (`backfit-model`), and sync live results (`fetch-live`).
*   **Web Server (`WebServer.cpp`)**: An embedded single-header C++ HTTP server (`cpp-httplib`) serving standings, simulation dashboards, game impacts, and a "what-if" sandbox where users can lock game outcomes.

---

## 2. World Cup vs. NFL: Key Structural Differences

Translating an NFL tracker into a FIFA World Cup tracker requires adjusting for structural differences between American football and association football (soccer).

| Dimension | NFL Tracker (`nfl3`) | World Cup Tracker (`wc`) |
| :--- | :--- | :--- |
| **Tournament Format** | Two conferences (AFC/NFC), each split into 4 divisions. 32 teams. | **Group Stage** (round-robin) followed by a single-elimination **Knockout Stage**. |
| **Team Count & Groups** | 32 teams, fixed alignment. | Traditionally **32 teams** (8 groups of 4). Moving to **48 teams** in 2026 (12 groups of 4). |
| **Match Outcomes** | Win / Loss (Draws are rare in C++ model, bypassed during score generation). | **Win / Draw / Loss** are all common in the Group Stage. |
| **Point System** | Win percentage (Wins + 0.5 * Ties). | **3 points for a win, 1 point for a draw, 0 for a loss**. |
| **Tiebreaker Criteria** | Complex NFL rules: head-to-head, division record, common games, SOV, SOS. | Goal difference, goals scored, head-to-head, fair-play points, drawing of lots. |
| **Cross-Group Ties** | None. Standings are strictly within division/conference. | **48-team format requires comparing third-place teams** across different groups. |
| **Knockout Stage Overtime** | Sudden-death or overtime rules ending in a field goal/touchdown. | 30 minutes of extra time, followed by a **penalty shootout** (crucial for simulations). |
| **Venue Factors** | Strong home-field advantage (typically ~57% home win rate). | **Neutral site tournaments** (except for the host nation(s), e.g., USA/Canada/Mexico). |

---

## 3. World Cup Standings & Tiebreakers

### 3.1 Group Standings
Within each group of 4 teams, standings are determined by the standard FIFA criteria:
1.  **Points** accumulated (3 for win, 1 for draw).
2.  **Goal difference** in all group matches.
3.  **Goals scored** in all group matches.
If two or more teams remain tied:
4.  **Points** obtained in the group matches between the teams concerned (head-to-head points).
5.  **Goal difference** from the matches between the teams concerned.
6.  **Goals scored** in the matches between the teams concerned.
7.  **Fair play points** (based on yellow and red cards: yellow = -1, indirect red = -3, direct red = -4, yellow + direct red = -5).
8.  **Drawing of lots** by the FIFA organizing committee.

### 3.2 Third-Place Ranking (48-Team Format)
In a 48-team World Cup (like 2026), the top 2 teams from each of the 12 groups advance, plus the **8 best third-place teams**. These third-place teams must be compared across different groups using:
1.  **Points** in group matches.
2.  **Goal difference** in group matches.
3.  **Goals scored** in group matches.
4.  **Fair play points** in all group matches.
5.  **Drawing of lots**.

---

## 4. Proposed Architectural Changes for `wc`

We can adapt the C++ architecture from `nfl3` into a `wc` (World Cup) program:

### 4.1 Data Schema Updates
*   **`data/teams.csv`**:
    ```csv
    abbreviation,full_name,group,elo_rating,federation
    ARG,Argentina,Group A,1860,CONMEBOL
    FRA,France,Group A,1840,UEFA
    ```
*   **`data/schedule.csv`**:
    ```csv
    match_id,stage,group,date,home_team,away_team,home_score,away_score,home_penalty_score,away_penalty_score,status
    1,group,A,2026-06-11,USA,BOL,-1,-1,-1,-1,scheduled
    49,knockout_r32,N/A,2026-06-27,TBD,TBD,-1,-1,-1,-1,scheduled
    ```

### 4.2 Simulation & Prediction Model
Instead of a simple logistic win/loss model, soccer matches require modeling draws and scorelines:
*   **Poisson Regression Model**: Model the goals scored by `Team A` and `Team B` as independent Poisson random variables:
    $$\text{Goals}_A \sim \text{Poisson}(\lambda_A), \quad \text{Goals}_B \sim \text{Poisson}(\lambda_B)$$
    Where $\lambda_A$ and $\lambda_B$ are calculated using team ratings (Elo):
    $$\ln(\lambda_A) = \text{base\_rate} + \alpha \cdot (\text{Elo}_A - \text{Elo}_B) + \text{host\_advantage (if host)}$$
*   **Knockout Stages**: If a simulated knockout match ends in a draw at 90 minutes, simulate:
    1.  Extra time (simulated via scaled Poisson rates for a 30-minute period).
    2.  If still tied, simulate a penalty shootout (50-50 random coin flip).

---

## 5. Confirmed Architectural Pillars

Based on user feedback, the design is defined as follows:
1.  **Format**: The program targets the **48-team format** (12 groups of 4; top 2 teams from each group + 8 best third-place teams advance to the Round of 32).
2.  **Simulation Model**: **Elo-based Poisson goal simulation** to model match scores and resolve goal-difference tiebreakers.
3.  **Data Ingestion & Calibration**:
    *   **Live scores**: Sync using a free scoreboard API (e.g., ESPN's soccer scoreboards).
    *   **Calibration**: Fit the Poisson parameters (`base_rate`, `alpha`, and `host_advantage`) using historical matches from past World Cups.
4.  **Tech Stack**: C++20 with CMake, `cpp-httplib`, and OpenMP for concurrency.
5.  **Host Advantage**: Apply a calibrated host nation advantage for the 2026 co-hosts (USA, Mexico, Canada) based on past World Cups.
6.  **Starting Strengths**: Pre-populate `data/teams.csv` with official FIFA points or World Football Elo ratings.
7.  **Penalty Shootouts**: Modeled as a simple **50-50 random coin flip** if a knockout match remains tied after extra time.
8.  **Knockout Matchups**: Implement the **official FIFA Round of 32 lookup algorithm** for third-place team allocation.
9.  **Fair Play Fallback**: Skip fair play score logging and **fall back directly to drawing of lots** if teams remain tied after points, goal difference, and goals scored.

---

## 6. Historical Calibration Setup

To facilitate Poisson model calibration:
*   We will store a historical dataset `data/historical/past_world_cups.csv` containing scores and Elo ratings of participating teams from past World Cups (e.g., 2002–2022).
*   The calibration command (`./wc backfit-model`) will optimize:
    *   $\text{base\_rate}$: Baseline goal rate per team.
    *   $\alpha$: Sensitivity of goal rate to team Elo rating differences.
    *   $\text{host\_advantage}$: Goal rate multiplier for host nations playing home games.
