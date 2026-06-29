#include "WebServer.h"
#include "model/MonteCarlo.h"
#include "util/CsvParser.h"
#include "model/Tournament.h"
#include "model/Tiebreaker.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <fstream>
#include <iomanip>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <netinet/in.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#include <set>
#include <random>

namespace {

std::string urlDecodeLocal(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            const std::string hex = value.substr(i + 1, 2);
            const char decoded = static_cast<char>(std::strtol(hex.c_str(), nullptr, 16));
            out += decoded;
            i += 2;
        } else if (value[i] == '+') {
            out += ' ';
        } else {
            out += value[i];
        }
    }
    return out;
}

std::map<std::string, std::string> parseUrlEncoded(const std::string& body) {
    std::map<std::string, std::string> values;
    std::stringstream ss(body);
    std::string part;
    while (std::getline(ss, part, '&')) {
        const size_t eq = part.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = urlDecodeLocal(part.substr(0, eq));
        const std::string value = urlDecodeLocal(part.substr(eq + 1));
        values[key] = value;
    }
    return values;
}

bool startsWith(const std::string& value, const std::string& prefix) {
  return value.rfind(prefix, 0) == 0;
}

bool parseInt(const std::string& value, int& out) {
  try {
    size_t idx = 0;
    const int parsed = std::stoi(value, &idx);
    if (idx != value.size()) {
      return false;
    }
    out = parsed;
    return true;
  } catch (...) {
    return false;
  }
}

bool isGroupFinalized(const Tournament& tournament, const std::string& group) {
  for (const auto& match : tournament.allMatches()) {
    if (match.stage() == "group" && match.group() == group && !match.isFinal()) {
      return false;
    }
  }
  return true;
}

const Match* findMatchById(const Tournament& tournament, int matchId) {
  for (const auto& match : tournament.allMatches()) {
    if (match.matchId() == matchId) {
      return &match;
    }
  }
  return nullptr;
}

std::string winnerOfFinalMatch(const Match* match) {
  if (!match || !match->isFinal()) {
    return "";
  }
  return match->homeTeamWon() ? match->homeTeam() : match->awayTeam();
}

std::string statusText(int statusCode) {
    switch (statusCode) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 500: return "Internal Server Error";
        default: return "OK";
    }
}

std::string readFullRequest(int clientFd) {
    std::string request;
    char buffer[4096];
    ssize_t bytesRead = 0;
    do {
        bytesRead = recv(clientFd, buffer, sizeof(buffer), 0);
        if (bytesRead > 0) {
            request.append(buffer, static_cast<size_t>(bytesRead));
        }
        if (bytesRead <= 0) break;

        const size_t headerEnd = request.find("\r\n\r\n");
        if (headerEnd == std::string::npos) continue;

        const std::string headers = request.substr(0, headerEnd);
        const size_t contentLengthPos = headers.find("Content-Length:");
        size_t expectedBody = 0;
        if (contentLengthPos != std::string::npos) {
            const size_t lineEnd = headers.find("\r\n", contentLengthPos);
            std::string lenField = headers.substr(
                contentLengthPos + std::strlen("Content-Length:"),
                lineEnd == std::string::npos ? std::string::npos : lineEnd - contentLengthPos - std::strlen("Content-Length:"));
            expectedBody = static_cast<size_t>(std::max(0, std::stoi(lenField)));
        }

        const size_t currentBody = request.size() - (headerEnd + 4);
        if (currentBody >= expectedBody) break;
    } while (bytesRead > 0);
    return request;
}

std::string buildDashboardHtml() {
    return R"rawhtml(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>2026 World Cup Tracker & Simulator</title>
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;700&family=Outfit:wght@400;500;600;700;800&display=swap" rel="stylesheet">
  <style>
    :root {
      --bg-primary: #080c14;
      --bg-secondary: #0f172a;
      --bg-surface: rgba(15, 23, 42, 0.65);
      --bg-surface-opaque: #0f172a;
      --border-color: rgba(255, 255, 255, 0.08);
      --text-primary: #f8fafc;
      --text-secondary: #94a3b8;
      --accent-color: #6366f1;
      --accent-gradient: linear-gradient(135deg, #4f46e5, #8b5cf6);
      --accent-glow: 0 0 15px rgba(99, 102, 241, 0.4);
      --success-color: #10b981;
      --success-bg: rgba(16, 185, 129, 0.15);
      --danger-color: #f43f5e;
      --danger-bg: rgba(244, 63, 94, 0.15);
      --warning-color: #f59e0b;
      --card-shadow: 0 8px 32px 0 rgba(0, 0, 0, 0.35);
      --font-display: 'Outfit', sans-serif;
      --font-body: 'Inter', sans-serif;
    }

    * {
      box-sizing: border-box;
      margin: 0;
      padding: 0;
    }

    body {
      background-color: var(--bg-primary);
      background-image: radial-gradient(circle at 80% 20%, rgba(99, 102, 241, 0.12) 0%, transparent 50%),
                        radial-gradient(circle at 10% 80%, rgba(139, 92, 246, 0.08) 0%, transparent 40%);
      background-attachment: fixed;
      color: var(--text-primary);
      font-family: var(--font-body);
      min-height: 100vh;
      line-height: 1.5;
    }

    header {
      background: rgba(8, 12, 20, 0.75);
      backdrop-filter: blur(12px);
      border-bottom: 1px solid var(--border-color);
      position: sticky;
      top: 0;
      z-index: 100;
      padding: 1rem 2rem;
    }

    .header-container {
      max-width: 1200px;
      margin: 0 auto;
      display: flex;
      justify-content: space-between;
      align-items: center;
      flex-wrap: wrap;
      gap: 1rem;
    }

    .logo-area {
      display: flex;
      align-items: center;
      gap: 0.5rem;
      text-decoration: none;
    }

    .logo-text {
      font-family: var(--font-display);
      font-weight: 800;
      font-size: 1.5rem;
      background: linear-gradient(135deg, #818cf8, #c084fc);
      -webkit-background-clip: text;
      -webkit-text-fill-color: transparent;
      letter-spacing: -0.04em;
    }

    .logo-sub {
      font-family: var(--font-display);
      font-weight: 500;
      font-size: 0.75rem;
      text-transform: uppercase;
      letter-spacing: 0.2em;
      color: var(--text-secondary);
      border-left: 1px solid var(--border-color);
      padding-left: 0.5rem;
    }

    nav {
      display: flex;
      gap: 0.5rem;
    }

    .nav-btn {
      background: transparent;
      border: 1px solid transparent;
      color: var(--text-secondary);
      font-family: var(--font-display);
      font-weight: 500;
      padding: 0.5rem 1rem;
      border-radius: 8px;
      cursor: pointer;
      text-decoration: none;
      transition: all 0.2s ease-in-out;
      font-size: 0.95rem;
    }

    .nav-btn:hover {
      color: var(--text-primary);
      background: rgba(255, 255, 255, 0.05);
    }

    .nav-btn.active {
      color: var(--text-primary);
      background: rgba(99, 102, 241, 0.2);
      border-color: rgba(99, 102, 241, 0.4);
      box-shadow: 0 0 12px rgba(99, 102, 241, 0.15);
    }

    main {
      max-width: 1200px;
      margin: 2rem auto;
      padding: 0 1.5rem 4rem 1.5rem;
    }

    .view-section {
      display: none;
      animation: fadeIn 0.3s ease-in-out forwards;
    }

    .view-section.active {
      display: block;
    }

    @keyframes fadeIn {
      from { opacity: 0; transform: translateY(8px); }
      to { opacity: 1; transform: translateY(0); }
    }

    .dashboard-header {
      margin-bottom: 2rem;
      display: flex;
      justify-content: space-between;
      align-items: center;
      flex-wrap: wrap;
      gap: 1.5rem;
    }

    .page-title {
      font-family: var(--font-display);
      font-size: 2rem;
      font-weight: 700;
      letter-spacing: -0.02em;
    }

    .page-desc {
      color: var(--text-secondary);
      font-size: 0.95rem;
      margin-top: 0.25rem;
    }

    .control-panel {
      background: var(--bg-surface);
      backdrop-filter: blur(16px);
      border: 1px solid var(--border-color);
      border-radius: 12px;
      padding: 1rem 1.5rem;
      display: flex;
      align-items: center;
      gap: 1.5rem;
      flex-wrap: wrap;
      margin-bottom: 2rem;
      box-shadow: var(--card-shadow);
    }

    .form-input {
      background: rgba(0, 0, 0, 0.25);
      border: 1px solid var(--border-color);
      color: var(--text-primary);
      padding: 0.6rem 1rem;
      border-radius: 8px;
      font-family: var(--font-body);
      font-size: 0.9rem;
      transition: all 0.2s ease;
    }

    .form-input:focus {
      outline: none;
      border-color: var(--accent-color);
      box-shadow: 0 0 8px rgba(99, 102, 241, 0.3);
    }

    .btn {
      font-family: var(--font-display);
      font-weight: 600;
      font-size: 0.9rem;
      padding: 0.6rem 1.2rem;
      border-radius: 8px;
      cursor: pointer;
      transition: all 0.2s ease;
      display: inline-flex;
      align-items: center;
      gap: 0.5rem;
      border: 1px solid transparent;
    }

    .btn-primary {
      background: var(--accent-gradient);
      color: white;
      box-shadow: var(--accent-glow);
    }

    .btn-primary:hover {
      opacity: 0.9;
      transform: translateY(-1px);
    }

    .btn-secondary {
      background: rgba(255, 255, 255, 0.05);
      color: var(--text-primary);
      border-color: var(--border-color);
    }

    .btn-secondary:hover {
      background: rgba(255, 255, 255, 0.1);
    }

    .group-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
      gap: 1.5rem;
      margin-bottom: 2rem;
    }

    .card {
      background: var(--bg-surface);
      backdrop-filter: blur(16px);
      border: 1px solid var(--border-color);
      border-radius: 16px;
      padding: 1.25rem;
      box-shadow: var(--card-shadow);
      transition: all 0.25s cubic-bezier(0.4, 0, 0.2, 1);
    }

    .card:hover {
      transform: translateY(-2px);
      border-color: rgba(99, 102, 241, 0.2);
    }

    .card-title {
      font-family: var(--font-display);
      font-size: 1.15rem;
      font-weight: 700;
      margin-bottom: 1rem;
      border-bottom: 1px solid var(--border-color);
      padding-bottom: 0.5rem;
      color: #cbd5e1;
      display: flex;
      justify-content: space-between;
      align-items: center;
    }

    table {
      width: 100%;
      border-collapse: collapse;
      font-size: 0.85rem;
    }

    th, td {
      padding: 0.4rem 0.5rem;
      text-align: left;
      border-bottom: 1px solid rgba(255, 255, 255, 0.03);
    }

    th {
      font-family: var(--font-display);
      font-weight: 600;
      color: var(--text-secondary);
      font-size: 0.75rem;
      text-transform: uppercase;
      letter-spacing: 0.05em;
    }

    td {
      font-weight: 500;
    }

    .team-badge {
      font-family: var(--font-display);
      font-weight: 700;
      padding: 0.15rem 0.4rem;
      border-radius: 4px;
      font-size: 0.8rem;
      background: rgba(255,255,255,0.06);
    }

    .qualify-top2 {
      border-left: 3px solid var(--success-color);
    }

    .qualify-3rd {
      border-left: 3px solid var(--warning-color);
    }

    .progress-container {
      display: flex;
      align-items: center;
      gap: 0.5rem;
    }

    .progress-bar-bg {
      width: 60px;
      height: 6px;
      background: rgba(255, 255, 255, 0.08);
      border-radius: 4px;
      overflow: hidden;
    }

    .progress-bar-fill {
      height: 100%;
      border-radius: 4px;
    }

    .fill-success { background: linear-gradient(90deg, #10b981, #34d399); }
    .fill-accent { background: linear-gradient(90deg, #6366f1, #818cf8); }

    .loading-overlay {
      display: none;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      padding: 4rem;
      text-align: center;
      gap: 1rem;
    }

    .spinner {
      width: 48px;
      height: 48px;
      border: 4px solid rgba(99, 102, 241, 0.1);
      border-top-color: var(--accent-color);
      border-radius: 50%;
      animation: spin 1s linear infinite;
    }

    @keyframes spin {
      to { transform: rotate(360deg); }
    }

    .matchup-grid {
      display: grid;
      grid-template-columns: repeat(auto-fill, minmax(320px, 1fr));
      gap: 1.5rem;
    }

    .matchup-card {
      background: var(--bg-surface);
      backdrop-filter: blur(16px);
      border: 1px solid var(--border-color);
      border-radius: 16px;
      padding: 1.25rem;
      box-shadow: var(--card-shadow);
      display: flex;
      flex-direction: column;
      gap: 1rem;
      position: relative;
    }

    .matchup-card::before {
      content: '';
      position: absolute;
      top: 0; left: 0; width: 4px; height: 100%;
      background: var(--accent-gradient);
    }

    .sandbox-grid {
      display: grid;
      grid-template-columns: 1.2fr 1fr;
      gap: 1.5rem;
      align-items: start;
    }

    @media (max-width: 900px) {
      .sandbox-grid { grid-template-columns: 1fr; }
    }

    .sandbox-games-list {
      display: flex;
      flex-direction: column;
      gap: 0.75rem;
      max-height: 70vh;
      overflow-y: auto;
      padding-right: 0.5rem;
    }

    .sandbox-game-card {
      background: rgba(255, 255, 255, 0.01);
      border: 1px solid var(--border-color);
      border-radius: 12px;
      padding: 0.75rem 1rem;
      display: flex;
      flex-direction: column;
      gap: 0.5rem;
    }

    .sandbox-team-btn {
      flex: 1;
      background: rgba(0, 0, 0, 0.25);
      border: 1px solid var(--border-color);
      color: var(--text-secondary);
      padding: 0.5rem;
      border-radius: 6px;
      cursor: pointer;
      font-size: 0.85rem;
      font-family: var(--font-display);
      font-weight: 600;
      transition: all 0.2s;
    }

    .sandbox-team-btn.active {
      background: var(--accent-gradient);
      color: white;
      border-color: transparent;
    }

    /* Modal */
    .modal-backdrop {
      position: fixed;
      top: 0; left: 0; width: 100%; height: 100%;
      background: rgba(0,0,0,0.85);
      backdrop-filter: blur(8px);
      z-index: 200;
      display: none;
      align-items: center;
      justify-content: center;
    }

    .modal-content {
      background: var(--bg-secondary);
      border: 1px solid var(--border-color);
      border-radius: 16px;
      max-width: 400px;
      width: 100%;
      padding: 1.5rem;
    }

    .modal-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 1.25rem;
    }

    /* Bracket styles */
    .bracket-wrapper {
      overflow: auto;
      max-height: 80vh;
      padding: 1rem 0;
    }

    .bracket-inner {
      min-width: 1400px;
      padding: 1rem;
    }

    .bracket-headers {
      display: flex;
      gap: 3rem;
      margin-bottom: 2rem;
      padding: 0 1rem;
    }

    .bracket-header-col {
      width: 250px;
      flex-shrink: 0;
      text-align: center;
      font-family: var(--font-display);
      font-weight: 700;
      font-size: 1.05rem;
      color: var(--text-primary);
      padding-bottom: 0.5rem;
      border-bottom: 2px solid var(--accent-color);
      text-transform: uppercase;
      letter-spacing: 0.05em;
    }

    .bracket-container {
      display: flex;
      gap: 3rem;
      height: 2400px;
      padding: 1rem;
    }

    .bracket-column {
      display: flex;
      flex-direction: column;
      justify-content: space-around;
      width: 250px;
      flex-shrink: 0;
      position: relative;
      height: 100%;
    }

    .bracket-matchup {
      display: flex;
      flex-direction: column;
      justify-content: space-around;
      flex: 1;
      position: relative;
    }

    /* Vertical connector line on the right side of the matchup pair */
    .bracket-matchup::after {
      content: "";
      position: absolute;
      right: -1.5rem;
      top: 25%;
      bottom: 25%;
      width: 1.5rem;
      border: 2px solid var(--border-color);
      border-left: none;
      pointer-events: none;
    }

    /* Horizontal line going from the vertical connector to the next round's card */
    .bracket-matchup::before {
      content: "";
      position: absolute;
      right: -3rem;
      top: 50%;
      width: 1.5rem;
      height: 2px;
      background: var(--border-color);
      pointer-events: none;
    }

    .bracket-game-card {
      background: var(--bg-surface);
      backdrop-filter: blur(12px);
      border: 1px solid var(--border-color);
      border-radius: 12px;
      padding: 0.75rem;
      box-shadow: var(--card-shadow);
      transition: all 0.2s ease-in-out;
      cursor: pointer;
      position: relative;
      z-index: 10;
    }

    .bracket-game-card:hover {
      transform: translateY(-2px);
      border-color: var(--accent-color);
      box-shadow: 0 0 15px rgba(99, 102, 241, 0.25);
    }

    .bracket-game-meta {
      display: flex;
      justify-content: space-between;
      font-size: 0.75rem;
      color: var(--text-secondary);
      font-weight: 500;
      margin-bottom: 0.5rem;
      border-bottom: 1px solid rgba(255, 255, 255, 0.05);
      padding-bottom: 0.25rem;
    }

    .bracket-game-team {
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding: 0.25rem 0;
      font-size: 0.85rem;
      font-weight: 500;
      color: var(--text-secondary);
    }

    .bracket-game-team.winner {
      color: var(--text-primary);
      font-weight: 700;
    }

    .bracket-game-team.winner .team-score-badge {
      background: var(--success-bg);
      color: var(--success-color);
      border: 1px solid rgba(16, 185, 129, 0.3);
    }

    .bracket-game-team.loser {
      opacity: 0.55;
    }

    .team-name-container {
      display: flex;
      align-items: center;
      gap: 0.5rem;
      overflow: hidden;
      white-space: nowrap;
      text-overflow: ellipsis;
    }

    .team-flag-text {
      font-weight: 700;
      font-size: 0.8rem;
      background: rgba(255, 255, 255, 0.05);
      padding: 0.1rem 0.35rem;
      border-radius: 4px;
      color: var(--text-primary);
      min-width: 2.2rem;
      text-align: center;
    }

    .team-score-badge {
      font-family: var(--font-display);
      font-weight: 700;
      background: rgba(255, 255, 255, 0.05);
      border: 1px solid var(--border-color);
      padding: 0.1rem 0.5rem;
      border-radius: 6px;
      font-size: 0.85rem;
      min-width: 1.5rem;
      text-align: center;
    }

    .third-place-container {
      margin-top: 2.5rem;
      border-top: 1px solid var(--border-color);
      padding-top: 2rem;
      text-align: center;
    }
  </style>
  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
</head>
<body>

  <header>
    <div class="header-container">
      <a href="/" class="logo-area">
        <span class="logo-text">wc26</span>
        <span class="logo-sub">simulator</span>
      </a>
      <nav>
        <button id="nav-standings" class="nav-btn" onclick="switchTab('standings')">Group Tables</button>
        <button id="nav-matches" class="nav-btn" onclick="switchTab('matches')">Matches</button>
        <button id="nav-elimination" class="nav-btn" onclick="switchTab('elimination')">Bracket</button>
        <button id="nav-simulation" class="nav-btn" onclick="switchTab('simulation')">Tournament Sim</button>
        <button id="nav-impact" class="nav-btn" onclick="switchTab('impact')">Match Importance</button>
        <button id="nav-sandbox" class="nav-btn" onclick="switchTab('sandbox')">What-If Sandbox</button>
        <button id="nav-history" class="nav-btn" onclick="switchTab('history')">Contender History</button>
      </nav>
    </div>
  </header>

  <main>
    <!-- STANDINGS TAB -->
    <section id="standings-section" class="view-section">
      <div class="dashboard-header">
        <div>
          <h1 class="page-title">World Cup Group Standings</h1>
          <p class="page-desc">Official standings for Groups A through L. Top 2 and best 8 third-place teams advance.</p>
        </div>
        <button class="btn btn-primary" onclick="openManualUpdate()">Enter Result</button>
      </div>
      <div id="standings-container" class="group-grid"></div>
    </section>

    <!-- MATCHES TAB -->
    <section id="matches-section" class="view-section">
      <div class="dashboard-header">
        <div>
          <h1 class="page-title">Fixtures & Results</h1>
          <p class="page-desc">Complete schedule of the tournament. Search by team and filter by stage. Click "Enter Score" on any scheduled match to update its result.</p>
        </div>
      </div>
      <div class="card" style="padding: 1.5rem; margin-bottom: 2rem;">
        <div style="margin-bottom: 1.5rem; display: flex; gap: 1rem; flex-wrap: wrap; justify-content: space-between; align-items: center;">
          <div style="display: flex; gap: 1rem; flex-wrap: wrap;">
            <input type="text" id="match-search" placeholder="Search by team (e.g. MEX)..." class="form-input" style="max-width: 300px; padding: 0.5rem 0.75rem; background: rgba(255,255,255,0.03); border: 1px solid var(--border-color); color: var(--text-primary); border-radius: 6px;" oninput="filterMatches()">
            <select id="match-filter-stage" class="form-input" style="max-width: 200px; padding: 0.5rem 0.75rem; background: rgba(255,255,255,0.03); border: 1px solid var(--border-color); color: var(--text-primary); border-radius: 6px; cursor: pointer;" onchange="filterMatches()">
              <option value="all">All Stages</option>
              <option value="group">Group Stage</option>
              <option value="knockout">Knockout Stage</option>
            </select>
          </div>
          <div style="display: flex; gap: 0.75rem; flex-wrap: wrap; align-items: center; background: rgba(255,255,255,0.02); border: 1px solid var(--border-color); padding: 0.5rem 1rem; border-radius: 8px;">
            <span style="font-size: 0.8rem; color: var(--text-secondary); font-weight: 500;">Sync ESPN by Date:</span>
            <input type="date" id="sync-date-picker" class="form-input" style="max-width: 150px; padding: 0.35rem 0.5rem; background: rgba(0,0,0,0.2); border: 1px solid var(--border-color); color: var(--text-primary); border-radius: 6px; font-size: 0.8rem; cursor: pointer;">
            <button class="btn btn-primary" onclick="syncScoresFromApi()" style="padding: 0.35rem 0.75rem; font-size: 0.8rem;">Sync</button>
            <div id="sync-status-msg" style="font-size: 0.8rem; font-weight: 600; margin-left: 0.5rem;"></div>
          </div>
        </div>
        <div style="overflow-x: auto;">
          <table>
            <thead>
              <tr>
                <th>ID</th>
                <th>Stage/Group</th>
                <th>Date</th>
                <th>Matchup</th>
                <th>Score</th>
                <th>Status</th>
                <th>Action</th>
              </tr>
            </thead>
            <tbody id="matches-table-body"></tbody>
          </table>
        </div>
      </div>
    </section>

    <!-- ELIMINATION BRACKET TAB -->
    <section id="elimination-section" class="view-section">
      <div class="dashboard-header">
        <div>
          <h1 class="page-title">Elimination Bracket</h1>
          <p class="page-desc">Track and forecast the knockout matches from the Round of 32 down to the Final. Click any unplayed match card to enter scores.</p>
        </div>
      </div>
      <div id="bracket-container-parent" class="card" style="padding: 1.5rem; overflow-x: auto;">
        <div id="bracket-view" class="bracket-wrapper">
          <!-- Columns will be dynamically inserted here -->
        </div>
      </div>
    </section>

    <!-- SIMULATION TAB -->
    <section id="simulation-section" class="view-section">
      <div class="dashboard-header">
        <div>
          <h1 class="page-title">Monte Carlo Tournament Forecaster</h1>
          <p class="page-desc">Runs 100,000 simulations using the Elo-based Poisson goals model to calculate advancement odds.</p>
        </div>
      </div>
      <div class="control-panel">
        <button class="btn btn-primary" onclick="runSimulation()">Run Forecast</button>
      </div>
      <div id="sim-loading" class="loading-overlay">
        <div class="spinner"></div>
        <p class="loading-text">Simulating remaining fixtures...</p>
      </div>
      <div id="sim-results" class="card" style="display:none">
        <table id="sim-table">
          <thead>
            <tr>
              <th>Team</th>
              <th>Adv R32 %</th>
              <th>Reach R16 %</th>
              <th>Reach QF %</th>
              <th>Reach SF %</th>
              <th>Reach Final %</th>
              <th>Champion %</th>
            </tr>
          </thead>
          <tbody id="sim-table-body"></tbody>
        </table>
      </div>
    </section>

    <!-- IMPACT TAB -->
    <section id="impact-section" class="view-section">
      <div class="dashboard-header">
        <div>
          <h1 class="page-title">Match Progression Impact</h1>
          <p class="page-desc">Analyzes how upcoming Group Stage matches impact each team's odds of reaching the Round of 32.</p>
        </div>
      </div>
      <div id="impact-loading" class="loading-overlay">
        <div class="spinner"></div>
        <p class="loading-text">Analyzing matchups...</p>
      </div>
      <div id="impact-container" class="matchup-grid"></div>
    </section>

    <!-- SANDBOX TAB -->
    <section id="sandbox-section" class="view-section">
      <div class="dashboard-header">
        <div>
          <h1 class="page-title">What-If Sandbox</h1>
          <p class="page-desc">Lock specific scores or outcomes for unplayed matches and forecast updated odds.</p>
        </div>
      </div>
      <div class="sandbox-grid">
        <div>
          <h2>Unplayed Fixtures</h2>
          <div id="sandbox-list" class="sandbox-games-list"></div>
        </div>
        <div>
          <div class="card">
            <div class="card-title">Sandboxed Progression Odds</div>
            <button class="btn btn-primary" onclick="runSandboxSim()" style="width:100%;margin-bottom:1rem">Run Sandbox Forecast</button>
            
            <div style="display:flex;gap:0.5rem;margin-bottom:1rem;flex-wrap:wrap;">
              <span style="font-size:0.8rem;color:var(--text-secondary);align-self:center;margin-right:0.5rem;">Visible Columns:</span>
              <button id="toggle-col-r32" class="btn btn-primary" style="padding:0.25rem 0.5rem;font-size:0.75rem;" onclick="toggleSandboxCol('r32')">R32</button>
              <button id="toggle-col-r16" class="btn btn-primary" style="padding:0.25rem 0.5rem;font-size:0.75rem;" onclick="toggleSandboxCol('r16')">R16</button>
              <button id="toggle-col-qf" class="btn btn-primary" style="padding:0.25rem 0.5rem;font-size:0.75rem;" onclick="toggleSandboxCol('qf')">QF</button>
              <button id="toggle-col-sf" class="btn btn-primary" style="padding:0.25rem 0.5rem;font-size:0.75rem;" onclick="toggleSandboxCol('sf')">SF</button>
              <button id="toggle-col-final" class="btn btn-primary" style="padding:0.25rem 0.5rem;font-size:0.75rem;" onclick="toggleSandboxCol('final')">Final</button>
              <button id="toggle-col-champion" class="btn btn-primary" style="padding:0.25rem 0.5rem;font-size:0.75rem;" onclick="toggleSandboxCol('champion')">Champ</button>
            </div>

            <div id="sandbox-loading" class="loading-overlay" style="padding:1rem">
              <div class="spinner" style="width:24px;height:24px"></div>
            </div>
            <table id="sandbox-table">
              <thead>
                <tr>
                  <th style="cursor:pointer;" onclick="setSandboxSort('abbr')">Team <span id="sort-ind-abbr"></span></th>
                  <th id="th-col-r32" style="cursor:pointer;" onclick="setSandboxSort('r32')">Adv R32 % <span id="sort-ind-r32"></span></th>
                  <th id="th-col-r16" style="cursor:pointer;" onclick="setSandboxSort('r16')">R16 % <span id="sort-ind-r16"></span></th>
                  <th id="th-col-qf" style="cursor:pointer;" onclick="setSandboxSort('qf')">QF % <span id="sort-ind-qf"></span></th>
                  <th id="th-col-sf" style="cursor:pointer;" onclick="setSandboxSort('sf')">SF % <span id="sort-ind-sf"></span></th>
                  <th id="th-col-final" style="cursor:pointer;" onclick="setSandboxSort('final')">Final % <span id="sort-ind-final"></span></th>
                  <th id="th-col-champion" style="cursor:pointer;" onclick="setSandboxSort('champion')">Champion % <span id="sort-ind-champion">▼</span></th>
                </tr>
              </thead>
              <tbody id="sandbox-table-body"></tbody>
            </table>
          </div>
        </div>
      </div>
    </section>

    <!-- CONTENDER HISTORY TAB -->
    <section id="history-section" class="view-section">
      <div class="dashboard-header">
        <div>
          <h1 class="page-title">Contender Probability History</h1>
          <p class="page-desc">Track how each team's probability of reaching the final shifts over checkpoints throughout the tournament.</p>
        </div>
        <div style="display: flex; gap: 1rem; align-items: center;">
          <div id="history-rebuild-status" style="font-size: 0.9rem; color: var(--text-secondary);"></div>
          <button id="btn-rebuild-history" class="btn btn-primary" onclick="rebuildHistoryData()">
            <span id="rebuild-btn-text">Rebuild History (100k Sims)</span>
          </button>
        </div>
      </div>
      
      <div class="card" style="padding: 1.5rem; margin-bottom: 2rem;">
        <div style="position: relative; height: 450px; width: 100%; margin-bottom: 2rem;">
          <canvas id="history-chart"></canvas>
        </div>
        
        <div style="display: flex; flex-direction: column; gap: 1rem;">
          <div style="display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 0.5rem;">
            <h3 style="font-family: var(--font-display); font-size: 1.1rem; color: #cbd5e1; margin: 0;">Select Teams to Display</h3>
            <div style="display: flex; gap: 0.5rem; flex-wrap: wrap;">
              <button class="btn btn-secondary" style="padding: 0.35rem 0.75rem; font-size: 0.8rem;" onclick="selectQuickGroup('contenders')">Reset to Top 5</button>
              <button class="btn btn-secondary" style="padding: 0.35rem 0.75rem; font-size: 0.8rem;" onclick="selectQuickGroup('all')">Select All</button>
              <button class="btn btn-secondary" style="padding: 0.35rem 0.75rem; font-size: 0.8rem;" onclick="selectQuickGroup('still_alive')">Still Alive</button>
              <button class="btn btn-secondary" style="padding: 0.35rem 0.75rem; font-size: 0.8rem;" onclick="selectQuickGroup('none')">Clear</button>
            </div>
          </div>
          
          <div id="history-group-buttons" style="display: flex; flex-wrap: wrap; gap: 0.4rem; padding: 0.5rem; background: rgba(255,255,255,0.02); border: 1px solid var(--border-color); border-radius: 8px; align-items: center;">
            <!-- Group buttons will be dynamically inserted here -->
          </div>
          
          <div id="history-team-pills" style="display: flex; flex-wrap: wrap; gap: 0.5rem; max-height: 200px; overflow-y: auto; padding: 0.75rem; border: 1px solid var(--border-color); border-radius: 8px; background: rgba(0,0,0,0.2);">
            <!-- Team checkboxes will be dynamically inserted here -->
          </div>
        </div>
      </div>
    </section>
  </main>

  <!-- UPDATE RESULT MODAL -->
  <div id="update-modal" class="modal-backdrop">
    <div class="modal-content">
      <div class="modal-header">
        <div>
          <h2 style="font-family:var(--font-display); margin-bottom: 0.25rem;">Enter Score</h2>
          <div id="modal-match-title" style="font-size: 0.85rem; font-weight: 600; color: var(--accent-color);"></div>
        </div>
        <button class="modal-close" onclick="closeUpdateModal()">&times;</button>
      </div>
      <form id="update-form" onsubmit="submitScore(event)">
        <div style="margin-bottom:1rem">
          <label class="form-input" style="display:block;margin-bottom:0.5rem">Match ID</label>
          <input type="number" id="form-match-id" class="form-input" style="width:100%" required>
        </div>
        <div style="display:flex;gap:1rem;margin-bottom:1rem">
          <div style="flex:1">
            <label class="form-input" style="display:block;margin-bottom:0.5rem">Home Score</label>
            <input type="number" id="form-home-score" class="form-input" style="width:100%" min="0" required>
          </div>
          <div style="flex:1">
            <label class="form-input" style="display:block;margin-bottom:0.5rem">Away Score</label>
            <input type="number" id="form-away-score" class="form-input" style="width:100%" min="0" required>
          </div>
        </div>
        <div style="display:flex;gap:1rem;margin-bottom:1rem">
          <div style="flex:1">
            <label class="form-input" style="display:block;margin-bottom:0.5rem">Home Penalty (opt)</label>
            <input type="number" id="form-home-penalty" class="form-input" style="width:100%" min="0">
          </div>
          <div style="flex:1">
            <label class="form-input" style="display:block;margin-bottom:0.5rem">Away Penalty (opt)</label>
            <input type="number" id="form-away-penalty" class="form-input" style="width:100%" min="0">
          </div>
        </div>
        <button type="submit" class="btn btn-primary" style="width:100%">Save Result</button>
      </form>
    </div>
  </div>

  <script>
    let activeTab = 'standings';
    let locks = {}; // matchId -> outcome ("home", "draw", "away")
    let allMatchesData = [];

    // Sandbox state
    let sandboxTeamsData = [];
    let sandboxSortCol = 'champion';
    let sandboxSortDesc = true;
    let sandboxColsVisible = { r32: true, r16: true, qf: true, sf: true, final: true, champion: true };

    function switchTab(tab) {
      document.querySelectorAll('.nav-btn').forEach(btn => btn.classList.remove('active'));
      document.querySelectorAll('.view-section').forEach(sec => sec.classList.remove('active'));
      
      document.getElementById('nav-' + tab).classList.add('active');
      document.getElementById(tab + '-section').classList.add('active');
      activeTab = tab;

      if (tab === 'standings') loadStandings();
      else if (tab === 'matches') loadMatchesList();
      else if (tab === 'elimination') loadEliminationBracket();
      else if (tab === 'simulation') runSimulation();
      else if (tab === 'impact') runImpactAnalysis();
      else if (tab === 'sandbox') loadSandboxFixtures();
      else if (tab === 'history') loadHistoryTab();
    }

    async function loadMatchesList() {
      const res = await fetch('/api/games');
      const data = await res.json();
      allMatchesData = data.games || [];
      renderMatchesTable(allMatchesData);
    }

    async function syncScoresFromApi() {
      const dateVal = document.getElementById('sync-date-picker').value; // YYYY-MM-DD
      const statusMsg = document.getElementById('sync-status-msg');
      
      statusMsg.innerText = 'Syncing...';
      statusMsg.style.color = 'var(--text-secondary)';
      
      try {
        let url = '/api/sync-scores';
        if (dateVal) {
          url += '?date=' + dateVal;
        }
        
        const res = await fetch(url);
        const data = await res.json();
        
        if (data.ok) {
          statusMsg.innerText = 'Sync OK!';
          statusMsg.style.color = 'var(--success-color)';
          loadStandings();
          if (activeTab === 'matches') loadMatchesList();
          setTimeout(() => { statusMsg.innerText = ''; }, 3000);
        } else {
          statusMsg.innerText = 'Error';
          statusMsg.style.color = 'var(--danger-color)';
          alert("Sync error: " + data.error);
        }
      } catch (e) {
        statusMsg.innerText = 'Failed';
        statusMsg.style.color = 'var(--danger-color)';
      }
    }

    function renderMatchesTable(matches) {
      const tbody = document.getElementById('matches-table-body');
      tbody.innerHTML = '';

      const isResolvedTeamName = (team) => {
        return !/^((WINNER|RUNNER_UP)_GROUP_[A-L]|THIRD_GROUP_[A-Z_]+|R32_WINNER_\d+|R16_WINNER_\d+|QUARTERFINAL_WINNER_\d+|SEMIFINAL_WINNER_\d+|TBD(_\d+)?)$/.test(team);
      };

      const visibleMatches = matches.filter(m => {
        if (m.stage === 'group') return true;
        return isResolvedTeamName(m.home_team) && isResolvedTeamName(m.away_team);
      });

      visibleMatches.forEach(m => {
        let row = document.createElement('tr');
        
        let scoreStr = '-';
        let actionBtn = '';
        
        if (m.status === 'final') {
          scoreStr = `${m.home_score} - ${m.away_score}`;
        } else {
          actionBtn = `<button class="btn btn-primary" style="padding: 0.25rem 0.5rem; font-size: 0.75rem;" onclick="triggerUpdateScore(${m.match_id}, '${m.home_team}', '${m.away_team}')">Enter Score</button>`;
        }
        
        let stageDisplay = m.stage.replace('_', ' ');
        let groupDisplay = m.group !== 'N/A' ? `Group ${m.group}` : '';
        
        row.innerHTML = `
          <td><strong>#${m.match_id}</strong></td>
          <td><span style="text-transform: uppercase; font-size: 0.75rem; color: var(--text-secondary);">${stageDisplay} ${groupDisplay}</span></td>
          <td>${m.date}<br><small style="color: var(--text-secondary); font-size: 0.7rem;">${m.host_city || ''}</small></td>
          <td><span class="team-badge">${m.home_team}</span> vs <span class="team-badge">${m.away_team}</span></td>
          <td><strong>${scoreStr}</strong></td>
          <td><span style="font-size: 0.75rem; text-transform: uppercase; color: ${m.status === 'final' ? 'var(--success-color)' : 'var(--text-secondary)'}">${m.status}</span></td>
          <td>${actionBtn}</td>
        `;
        tbody.appendChild(row);
      });
    }

    function filterMatches() {
      const query = document.getElementById('match-search').value.toLowerCase().trim();
      const stage = document.getElementById('match-filter-stage').value;
      
      const filtered = allMatchesData.filter(m => {
        const matchesQuery = query === '' || 
                             m.home_team.toLowerCase().includes(query) || 
                             m.away_team.toLowerCase().includes(query);
                             
        let matchesStage = true;
        if (stage === 'group') {
          matchesStage = m.stage === 'group';
        } else if (stage === 'knockout') {
          matchesStage = m.stage !== 'group';
        }
        
        return matchesQuery && matchesStage;
      });
      
      renderMatchesTable(filtered);
    }

    async function loadStandings() {
      const res = await fetch('/api/standings');
      const data = await res.json();
      const container = document.getElementById('standings-container');
      container.innerHTML = '';

      for (const [group, teams] of Object.entries(data.groups)) {
        let card = document.createElement('div');
        card.className = 'card';
        let rowsHtml = teams.map((t, idx) => {
          let styleClass = idx < 2 ? 'qualify-top2' : (t.adv_3rd ? 'qualify-3rd' : '');
          let abbrHtml = `<span class="team-badge">${t.abbr}</span>`;
          if (t.clinched_status !== "") {
             let badgeColor = t.clinched_status === "ELIMINATED" ? "var(--danger-color)" : "var(--success-color)";
             abbrHtml += ` <span style="font-size: 0.65rem; color: ${badgeColor}; border: 1px solid ${badgeColor}; padding: 1px 4px; border-radius: 4px; margin-left: 4px; vertical-align: middle;">${t.clinched_status}</span>`;
          }
          return `<tr class="${styleClass}">
            <td>${abbrHtml}</td>
            <td>${t.pts}</td>
            <td>${t.gd}</td>
            <td>${t.gf}</td>
          </tr>`;
        }).join('');

        card.innerHTML = `<div class="card-title">Group ${group}</div>
          <table>
            <thead>
              <tr>
                <th>Team</th>
                <th>Pts</th>
                <th>GD</th>
                <th>GF</th>
              </tr>
            </thead>
            <tbody>${rowsHtml}</tbody>
          </table>`;
        container.appendChild(card);
      }
    }

    async function runSimulation() {
      const loading = document.getElementById('sim-loading');
      const results = document.getElementById('sim-results');
      loading.style.display = 'flex';
      results.style.display = 'none';

      const res = await fetch('/api/simulation?iterations=100000');
      const data = await res.json();
      loading.style.display = 'none';
      results.style.display = 'block';

      const tbody = document.getElementById('sim-table-body');
      tbody.innerHTML = '';

      let teams = Object.keys(data.r32).map(abbr => ({
        abbr,
        r32: data.r32[abbr],
        r16: data.r16[abbr],
        qf: data.qf[abbr],
        sf: data.sf[abbr],
        final: data.final[abbr],
        champion: data.champion[abbr]
      }));

      // Sort by champion odds, then R32 odds
      teams.sort((a, b) => b.champion - a.champion || b.r32 - a.r32);

      teams.forEach(t => {
        let row = document.createElement('tr');
        row.innerHTML = `
          <td><strong>${t.abbr}</strong></td>
          <td>${(t.r32 * 100).toFixed(1)}%</td>
          <td>${(t.r16 * 100).toFixed(1)}%</td>
          <td>${(t.qf * 100).toFixed(1)}%</td>
          <td>${(t.sf * 100).toFixed(1)}%</td>
          <td>${(t.final * 100).toFixed(1)}%</td>
          <td><span class="team-badge" style="background:var(--success-bg);color:var(--success-color)">${(t.champion * 100).toFixed(1)}%</span></td>
        `;
        tbody.appendChild(row);
      });
    }

    async function runImpactAnalysis() {
      const loading = document.getElementById('impact-loading');
      const container = document.getElementById('impact-container');
      loading.style.display = 'flex';
      container.innerHTML = '';

      const res = await fetch('/api/impact?iterations=10000');
      const data = await res.json();
      loading.style.display = 'none';

      data.impacts.forEach(imp => {
        let card = document.createElement('div');
        card.className = 'matchup-card';
        card.innerHTML = `
          <div style="font-family:var(--font-display);font-size:0.85rem;color:var(--text-secondary)">ID #${imp.match_id} &bull; ${imp.date}</div>
          <div style="display:flex;justify-content:space-between;align-items:center">
            <span style="font-size:1.2rem;font-weight:700">${imp.home_team}</span>
            <span style="color:var(--text-secondary)">vs</span>
            <span style="font-size:1.2rem;font-weight:700">${imp.away_team}</span>
          </div>
          <div style="display:flex;justify-content:space-between;font-size:0.8rem;color:var(--text-secondary)">
            <span>${imp.home_team} win: R32 Delta <strong>+${(imp.home_delta * 100).toFixed(1)}%</strong></span>
            <span>${imp.away_team} win: R32 Delta <strong>+${(imp.away_delta * 100).toFixed(1)}%</strong></span>
          </div>
        `;
        container.appendChild(card);
      });
    }

    async function loadSandboxFixtures() {
      const res = await fetch('/api/games');
      const data = await res.json();
      const list = document.getElementById('sandbox-list');
      list.innerHTML = '';

      // Only display scheduled/upcoming group matches
      const unplayed = data.games.filter(g => g.stage === 'group' && (g.status === 'scheduled' || g.status === 'upcoming'));

      unplayed.forEach(g => {
        let card = document.createElement('div');
        card.className = 'sandbox-game-card';
        let lockHome = locks[g.match_id] === 'home' ? 'active' : '';
        let lockDraw = locks[g.match_id] === 'draw' ? 'active' : '';
        let lockAway = locks[g.match_id] === 'away' ? 'active' : '';

        card.innerHTML = `
          <div class="sandbox-game-header">Match ID #${g.match_id} &bull; Group ${g.group} &bull; ${g.date} &bull; ${g.host_city || ''}</div>
          <div style="display:flex;gap:0.5rem">
            <button class="sandbox-team-btn ${lockHome}" onclick="setLock(${g.match_id}, 'home')">${g.home_team}</button>
            <button class="sandbox-team-btn ${lockDraw}" onclick="setLock(${g.match_id}, 'draw')">Draw</button>
            <button class="sandbox-team-btn ${lockAway}" onclick="setLock(${g.match_id}, 'away')">${g.away_team}</button>
          </div>
        `;
        list.appendChild(card);
      });
    }

    function setLock(matchId, outcome) {
      if (locks[matchId] === outcome) {
        delete locks[matchId];
      } else {
        locks[matchId] = outcome;
      }
      loadSandboxFixtures();
    }

    async function runSandboxSim() {
      const loading = document.getElementById('sandbox-loading');
      loading.style.display = 'block';

      // Convert locks to API parameters
      let lockList = [];
      for (const [id, outcome] of Object.entries(locks)) {
        lockList.push(id + ':' + outcome);
      }
      const locksParam = lockList.join(',');

      const res = await fetch('/api/simulation?iterations=100000&locks=' + encodeURIComponent(locksParam));
      const data = await res.json();
      loading.style.display = 'none';

      // Convert to array and include all stages
      let teams = Object.keys(data.r32).map(abbr => ({
        abbr,
        r32: data.r32[abbr],
        r16: data.r16[abbr],
        qf: data.qf[abbr],
        sf: data.sf[abbr],
        final: data.final[abbr],
        champion: data.champion[abbr]
      }));

      // Filter out mathematically eliminated teams
      sandboxTeamsData = teams.filter(t => 
        t.r32 > 0 || t.r16 > 0 || t.qf > 0 || t.sf > 0 || t.final > 0 || t.champion > 0
      );

      renderSandboxTable();
    }

    function setSandboxSort(col) {
      if (sandboxSortCol === col) {
        sandboxSortDesc = !sandboxSortDesc;
      } else {
        sandboxSortCol = col;
        sandboxSortDesc = true;
      }
      renderSandboxTable();
    }

    function toggleSandboxCol(col) {
      sandboxColsVisible[col] = !sandboxColsVisible[col];
      const btn = document.getElementById('toggle-col-' + col);
      if (sandboxColsVisible[col]) {
        btn.className = 'btn btn-primary';
      } else {
        btn.className = 'btn btn-secondary';
      }
      renderSandboxTable();
    }

    function renderSandboxTable() {
      const tbody = document.getElementById('sandbox-table-body');
      tbody.innerHTML = '';

      // Update header visibility
      const cols = ['r32', 'r16', 'qf', 'sf', 'final', 'champion'];
      cols.forEach(c => {
        document.getElementById('th-col-' + c).style.display = sandboxColsVisible[c] ? '' : 'none';
        document.getElementById('sort-ind-' + c).innerText = sandboxSortCol === c ? (sandboxSortDesc ? '▼' : '▲') : '';
      });
      document.getElementById('sort-ind-abbr').innerText = sandboxSortCol === 'abbr' ? (sandboxSortDesc ? '▼' : '▲') : '';

      sandboxTeamsData.sort((a, b) => {
        let valA = a[sandboxSortCol];
        let valB = b[sandboxSortCol];
        if (sandboxSortCol === 'abbr') {
          return sandboxSortDesc ? valB.localeCompare(valA) : valA.localeCompare(valB);
        }
        if (valA === valB) {
          // tiebreaker is champion or r32
          if (sandboxSortCol !== 'champion' && a.champion !== b.champion) return b.champion - a.champion;
          return b.r32 - a.r32;
        }
        return sandboxSortDesc ? (valB - valA) : (valA - valB);
      });

      sandboxTeamsData.forEach(t => {
        let row = document.createElement('tr');
        let html = `<td><strong>${t.abbr}</strong></td>`;
        if (sandboxColsVisible.r32) html += `<td>${(t.r32 * 100).toFixed(1)}%</td>`;
        if (sandboxColsVisible.r16) html += `<td>${(t.r16 * 100).toFixed(1)}%</td>`;
        if (sandboxColsVisible.qf) html += `<td>${(t.qf * 100).toFixed(1)}%</td>`;
        if (sandboxColsVisible.sf) html += `<td>${(t.sf * 100).toFixed(1)}%</td>`;
        if (sandboxColsVisible.final) html += `<td>${(t.final * 100).toFixed(1)}%</td>`;
        if (sandboxColsVisible.champion) html += `<td><span class="team-badge" style="background:var(--success-bg);color:var(--success-color)">${(t.champion * 100).toFixed(1)}%</span></td>`;
        
        row.innerHTML = html;
        tbody.appendChild(row);
      });
    }

    function openUpdateModal() {
      document.getElementById('update-modal').style.display = 'flex';
    }

    function closeUpdateModal() {
      document.getElementById('update-modal').style.display = 'none';
    }

    function openManualUpdate() {
      document.getElementById('form-match-id').value = '';
      document.getElementById('form-match-id').readOnly = false;
      document.getElementById('modal-match-title').innerText = 'Enter Score by Match ID';
      document.getElementById('form-home-score').value = '';
      document.getElementById('form-away-score').value = '';
      document.getElementById('form-home-penalty').value = '';
      document.getElementById('form-away-penalty').value = '';
      openUpdateModal();
    }

    function triggerUpdateScore(matchId, homeTeam, awayTeam) {
      document.getElementById('form-match-id').value = matchId;
      document.getElementById('form-match-id').readOnly = true;
      document.getElementById('modal-match-title').innerText = `${homeTeam} vs ${awayTeam} (Match #${matchId})`;
      document.getElementById('form-home-score').value = '';
      document.getElementById('form-away-score').value = '';
      document.getElementById('form-home-penalty').value = '';
      document.getElementById('form-away-penalty').value = '';
      openUpdateModal();
    }

    async function submitScore(event) {
      event.preventDefault();
      const id = document.getElementById('form-match-id').value;
      const home = document.getElementById('form-home-score').value;
      const away = document.getElementById('form-away-score').value;
      const homePen = document.getElementById('form-home-penalty').value;
      const awayPen = document.getElementById('form-away-penalty').value;

      if ((homePen !== '' || awayPen !== '') && home !== away) {
        alert("Penalty scores can only be applied when the regular score is tied.");
        return;
      }

      const res = await fetch('/api/update-result', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: 'match_id=' + id + '&home_score=' + home + '&away_score=' + away + '&home_penalty_score=' + homePen + '&away_penalty_score=' + awayPen
      });

      const data = await res.json();
      if (data.ok) {
        closeUpdateModal();
        loadStandings();
        if (activeTab === 'matches') loadMatchesList();
        if (activeTab === 'elimination') loadEliminationBracket();
      } else {
        alert("Error updating score: " + data.error);
      }
    }

    function formatTeamName(teamCode) {
      if (!teamCode) return "";
      if (teamCode.startsWith("WINNER_GROUP_")) {
        return "1" + teamCode.replace("WINNER_GROUP_", "");
      }
      if (teamCode.startsWith("RUNNER_UP_GROUP_")) {
        return "2" + teamCode.replace("RUNNER_UP_GROUP_", "");
      }
      if (teamCode.startsWith("THIRD_GROUP_")) {
        return "3rd";
      }
      if (teamCode.startsWith("R32_WINNER_")) {
        return "W" + teamCode.replace("R32_WINNER_", "");
      }
      if (teamCode.startsWith("R16_WINNER_")) {
        return "W" + teamCode.replace("R16_WINNER_", "");
      }
      if (teamCode.startsWith("QUARTERFINAL_WINNER_")) {
        return "W" + teamCode.replace("QUARTERFINAL_WINNER_", "");
      }
      if (teamCode.startsWith("SEMIFINAL_WINNER_")) {
        return "W" + teamCode.replace("SEMIFINAL_WINNER_", "");
      }
      if (teamCode.startsWith("TBD_")) {
        return "TBD";
      }
      return teamCode;
    }

    function formatTeamBadge(teamCode) {
      return formatTeamName(teamCode).substring(0, 3).toUpperCase();
    }

    function createBracketCardHtml(match) {
      const isFinal = match.status === 'final';
      
      let homeName = formatTeamName(match.home_team);
      const homeProb = match.home_win_prob !== undefined ? ` (${Math.round(match.home_win_prob * 100)}%)` : '';
      const homeDisplay = homeName + homeProb;
      const homeScore = isFinal ? match.home_score : '-';
      
      let awayName = formatTeamName(match.away_team);
      const awayProb = match.away_win_prob !== undefined ? ` (${Math.round(match.away_win_prob * 100)}%)` : '';
      const awayDisplay = awayName + awayProb;
      const awayScore = isFinal ? match.away_score : '-';

      let homeClass = 'bracket-game-team';
      let awayClass = 'bracket-game-team';
      if (isFinal) {
        const homeWon = match.home_score > match.away_score || 
                        (match.home_score === match.away_score && match.home_penalty_score > match.away_penalty_score);
        if (homeWon) {
          homeClass += ' winner';
          awayClass += ' loser';
        } else {
          awayClass += ' winner';
          homeClass += ' loser';
        }
      }

      let shootoutText = '';
      if (isFinal && match.home_score === match.away_score && match.home_penalty_score >= 0) {
        shootoutText = ` <span style="font-size: 0.7rem; color: var(--text-secondary);">(PEN ${match.home_penalty_score}-${match.away_penalty_score})</span>`;
      }

      return `
        <div class="bracket-game-meta">
          <span>Match #${match.match_id}</span>
          <span>${match.date}</span>
        </div>
        <div class="bracket-game-teams">
          <div class="${homeClass}">
            <div class="team-name-container">
              <span class="team-flag-text">${formatTeamBadge(match.home_team)}</span>
              <span>${homeDisplay}</span>
            </div>
            <span class="team-score-badge">${homeScore}</span>
          </div>
          <div class="${awayClass}">
            <div class="team-name-container">
              <span class="team-flag-text">${formatTeamBadge(match.away_team)}</span>
              <span>${awayDisplay}</span>
            </div>
            <span class="team-score-badge">${awayScore}</span>
          </div>
        </div>
        ${shootoutText ? '<div style="text-align: center; margin-top: 0.25rem;">' + shootoutText + '</div>' : ''}
      `;
    }

    function createBracketCard(match) {
      const card = document.createElement('div');
      card.className = 'bracket-game-card';
      card.innerHTML = createBracketCardHtml(match);
      if (match.status !== 'final') {
        card.onclick = () => triggerUpdateScore(match.match_id, match.home_team, match.away_team);
      }
      return card;
    }

    async function loadEliminationBracket() {
      const container = document.getElementById('bracket-view');
      container.innerHTML = '<div class="spinner" style="margin: 4rem auto;"></div>';

      try {
        const res = await fetch('/api/games');
        const data = await res.json();
        const matches = data.games || [];

        const matchMap = {};
        matches.forEach(m => {
          matchMap[m.match_id] = m;
        });

        // 5 columns of matches ordered sequentially to align branch endpoints:
        const rounds = [
          {
            name: "Round of 32",
            matches: [74, 77, 73, 75, 83, 84, 81, 82, 76, 78, 79, 80, 86, 88, 85, 87]
          },
          {
            name: "Round of 16",
            matches: [89, 90, 93, 94, 91, 92, 95, 96]
          },
          {
            name: "Quarterfinals",
            matches: [97, 98, 99, 100]
          },
          {
            name: "Semifinals",
            matches: [101, 102]
          },
          {
            name: "Final",
            matches: [104]
          }
        ];

        container.innerHTML = '';
        const innerContainer = document.createElement('div');
        innerContainer.className = 'bracket-inner';

        const headersRow = document.createElement('div');
        headersRow.className = 'bracket-headers';
        rounds.forEach(round => {
          const headerCol = document.createElement('div');
          headerCol.className = 'bracket-header-col';
          headerCol.innerText = round.name;
          headersRow.appendChild(headerCol);
        });
        innerContainer.appendChild(headersRow);

        const bracketContainer = document.createElement('div');
        bracketContainer.className = 'bracket-container';

        rounds.forEach((round, roundIdx) => {
          const column = document.createElement('div');
          column.className = 'bracket-column';

          if (roundIdx < rounds.length - 1) {
            for (let i = 0; i < round.matches.length; i += 2) {
              const matchupDiv = document.createElement('div');
              matchupDiv.className = 'bracket-matchup';

              const matchId1 = round.matches[i];
              const matchId2 = round.matches[i+1];

              const match1 = matchMap[matchId1];
              const match2 = matchMap[matchId2];

              if (match1) matchupDiv.appendChild(createBracketCard(match1));
              if (match2) matchupDiv.appendChild(createBracketCard(match2));

              column.appendChild(matchupDiv);
            }
          } else {
            const matchId = round.matches[0];
            const match = matchMap[matchId];
            if (match) {
              const finalWrapper = document.createElement('div');
              finalWrapper.style.display = 'flex';
              finalWrapper.style.flexDirection = 'column';
              finalWrapper.style.justifyContent = 'center';
              finalWrapper.style.height = '100%';
              finalWrapper.appendChild(createBracketCard(match));
              column.appendChild(finalWrapper);
            }
          }

          bracketContainer.appendChild(column);
        });

        innerContainer.appendChild(bracketContainer);
        container.appendChild(innerContainer);

        const thirdPlaceMatch = matchMap[103];
        if (thirdPlaceMatch) {
          const tpCard = createBracketCard(thirdPlaceMatch);
          tpCard.style.margin = '0 auto';
          tpCard.style.maxWidth = '250px';

          const thirdPlaceContainer = document.createElement('div');
          thirdPlaceContainer.className = 'third-place-container';

          const tpHeader = document.createElement('div');
          tpHeader.className = 'bracket-round-header';
          tpHeader.style.margin = '0 auto 1.5rem auto';
          tpHeader.style.maxWidth = '250px';
          tpHeader.style.borderColor = 'var(--warning-color)';
          tpHeader.innerText = 'Third Place Play-off';

          thirdPlaceContainer.appendChild(tpHeader);
          thirdPlaceContainer.appendChild(tpCard);
          container.appendChild(thirdPlaceContainer);
        }
      } catch (e) {
        container.innerHTML = `<div style="color:var(--danger-color); padding:2rem; text-align:center;">Failed to load bracket data: ${e.message}</div>`;
      }
    }

    let historyChart = null;
    let historyData = [];
    let selectedTeams = new Set();
    let groupTeamsMap = {};
    const checkpointLabels = {
      0: "Pre-Tournament",
      1: "1 Game",
      2: "2 Games",
      3: "3 Games",
      4: "4 Games",
      5: "5 Games",
      6: "6 Games",
      7: "7 Games",
      8: "8 Games"
    };

    function getTeamColor(abbr) {
      let hash = 0;
      for (let i = 0; i < abbr.length; i++) {
        hash = abbr.charCodeAt(i) + ((hash << 5) - hash);
      }
      const hue = Math.abs(hash) % 360;
      return {
        line: `hsla(${hue}, 85%, 65%, 1)`,
        fill: `hsla(${hue}, 85%, 65%, 0.1)`,
        border: `hsla(${hue}, 85%, 65%, 0.4)`
      };
    }

    async function loadHistoryTab() {
      const statusDiv = document.getElementById('history-rebuild-status');
      statusDiv.innerText = "Loading history data...";
      try {
        const [historyRes, standingsRes] = await Promise.all([
          fetch('/api/probability-history'),
          fetch('/api/standings')
        ]);
        historyData = await historyRes.json();
        const standingsData = await standingsRes.json();
        statusDiv.innerText = "";
        
        if (historyData.error) {
          statusDiv.innerText = "Error: " + historyData.error;
          return;
        }

        // Build groupTeamsMap
        groupTeamsMap = {};
        if (standingsData && standingsData.groups) {
          for (const [groupName, teams] of Object.entries(standingsData.groups)) {
            groupTeamsMap[groupName] = teams.map(t => t.abbr);
          }
        }
        
        renderGroupButtons();

        const uniqueCheckpoints = [...new Set(historyData.map(d => d.games_played))].sort((a, b) => a - b);
        const allTeams = [...new Set(historyData.map(d => d.team))].sort();

        renderTeamPills(allTeams);

        if (selectedTeams.size === 0 && uniqueCheckpoints.length > 0) {
          const latestData = [];
          allTeams.forEach(abbr => {
            const teamPoints = historyData.filter(d => d.team === abbr);
            if (teamPoints.length > 0) {
              const maxPoint = teamPoints.reduce((max, p) => p.games_played > max.games_played ? p : max, teamPoints[0]);
              latestData.push(maxPoint);
            }
          });
          latestData.sort((a, b) => b.probability - a.probability);
          for (let i = 0; i < 5 && i < latestData.length; i++) {
            selectedTeams.add(latestData[i].team);
          }
          allTeams.forEach(abbr => {
            const cb = document.getElementById('chk-team-' + abbr);
            if (cb) {
              cb.checked = selectedTeams.has(abbr);
              const label = cb.parentElement;
              if (selectedTeams.has(abbr)) {
                label.classList.add('active');
              } else {
                label.classList.remove('active');
              }
            }
          });
        }

        updateHistoryChart(uniqueCheckpoints);
      } catch (e) {
        statusDiv.innerText = "Failed to load history data.";
        console.error(e);
      }
    }

    function renderGroupButtons() {
      const container = document.getElementById('history-group-buttons');
      if (!container) return;
      container.innerHTML = '';
      
      const labelSpan = document.createElement('span');
      labelSpan.style.fontSize = '0.75rem';
      labelSpan.style.color = 'var(--text-secondary)';
      labelSpan.style.fontWeight = '600';
      labelSpan.style.display = 'flex';
      labelSpan.style.alignItems = 'center';
      labelSpan.style.marginRight = '0.5rem';
      labelSpan.innerText = 'Filter by Group:';
      container.appendChild(labelSpan);

      const sortedGroups = Object.keys(groupTeamsMap).sort();
      sortedGroups.forEach(groupName => {
        const btn = document.createElement('button');
        btn.className = 'btn btn-secondary';
        btn.style.padding = '0.25rem 0.5rem';
        btn.style.fontSize = '0.75rem';
        btn.innerText = `Group ${groupName}`;
        btn.onclick = () => selectQuickGroup(`group-${groupName}`);
        container.appendChild(btn);
      });
    }

    function renderTeamPills(teams) {
      const container = document.getElementById('history-team-pills');
      container.innerHTML = '';
      
      teams.forEach(abbr => {
        const colorInfo = getTeamColor(abbr);
        const label = document.createElement('label');
        label.style.display = 'inline-flex';
        label.style.alignItems = 'center';
        label.style.gap = '0.35rem';
        label.style.padding = '0.35rem 0.65rem';
        label.style.background = 'rgba(255, 255, 255, 0.03)';
        label.style.border = `1px solid var(--border-color)`;
        label.style.borderRadius = '20px';
        label.style.fontSize = '0.8rem';
        label.style.cursor = 'pointer';
        label.style.transition = 'all 0.2s ease';
        label.className = selectedTeams.has(abbr) ? 'active' : '';
        if (selectedTeams.has(abbr)) {
          label.style.borderColor = colorInfo.line;
          label.style.background = colorInfo.fill;
        }

        label.innerHTML = `
          <span style="display:inline-block; width:8px; height:8px; border-radius:50%; background-color:${colorInfo.line}"></span>
          <input type="checkbox" id="chk-team-${abbr}" style="display:none" ${selectedTeams.has(abbr) ? 'checked' : ''} onchange="toggleTeamSelection('${abbr}')">
          <strong>${abbr}</strong>
        `;
        container.appendChild(label);
      });
    }

    function toggleTeamSelection(abbr) {
      const cb = document.getElementById('chk-team-' + abbr);
      const label = cb.parentElement;
      const colorInfo = getTeamColor(abbr);

      if (cb.checked) {
        selectedTeams.add(abbr);
        label.classList.add('active');
        label.style.borderColor = colorInfo.line;
        label.style.background = colorInfo.fill;
      } else {
        selectedTeams.delete(abbr);
        label.classList.remove('active');
        label.style.borderColor = 'var(--border-color)';
        label.style.background = 'rgba(255, 255, 255, 0.03)';
      }

      const uniqueCheckpoints = [...new Set(historyData.map(d => d.games_played))].sort((a, b) => a - b);
      updateHistoryChart(uniqueCheckpoints);
    }

    function selectQuickGroup(groupType) {
      if (groupType === 'all') {
        const allTeams = [...new Set(historyData.map(d => d.team))];
        allTeams.forEach(abbr => selectedTeams.add(abbr));
      } else if (groupType === 'none') {
        selectedTeams.clear();
      } else if (groupType === 'still_alive') {
        selectedTeams.clear();
        const allTeams = [...new Set(historyData.map(d => d.team))];
        allTeams.forEach(abbr => {
          const teamPoints = historyData.filter(d => d.team === abbr);
          if (teamPoints.length > 0) {
            const maxPoint = teamPoints.reduce((max, p) => p.games_played > max.games_played ? p : max, teamPoints[0]);
            if (maxPoint.probability > 0) {
              selectedTeams.add(abbr);
            }
          }
        });
      } else if (groupType === 'contenders') {
        selectedTeams.clear();
        const allTeams = [...new Set(historyData.map(d => d.team))];
        const latestData = [];
        allTeams.forEach(abbr => {
          const teamPoints = historyData.filter(d => d.team === abbr);
          if (teamPoints.length > 0) {
            const maxPoint = teamPoints.reduce((max, p) => p.games_played > max.games_played ? p : max, teamPoints[0]);
            latestData.push(maxPoint);
          }
        });
        latestData.sort((a, b) => b.probability - a.probability);
        for (let i = 0; i < 5 && i < latestData.length; i++) {
          selectedTeams.add(latestData[i].team);
        }
      } else if (groupType.startsWith('group-')) {
        const groupName = groupType.substring(6);
        selectedTeams.clear();
        if (groupTeamsMap[groupName]) {
          groupTeamsMap[groupName].forEach(abbr => selectedTeams.add(abbr));
        }
      }
      
      const allTeams = [...new Set(historyData.map(d => d.team))].sort();
      renderTeamPills(allTeams);
      const uniqueCheckpoints = [...new Set(historyData.map(d => d.games_played))].sort((a, b) => a - b);
      updateHistoryChart(uniqueCheckpoints);
    }

    function updateHistoryChart(checkpoints) {
      const ctx = document.getElementById('history-chart').getContext('2d');
      const xLabels = checkpoints.map(cp => checkpointLabels[cp] || `${cp} Games`);

      const datasets = [];
      selectedTeams.forEach(abbr => {
        const teamData = [];
        checkpoints.forEach(cp => {
          const point = historyData.find(d => d.games_played === cp && d.team === abbr);
          teamData.push(point ? point.probability : null);
        });

        const colors = getTeamColor(abbr);
        datasets.push({
          label: abbr,
          data: teamData,
          borderColor: colors.line,
          backgroundColor: colors.fill,
          borderWidth: 3,
          pointBackgroundColor: colors.line,
          pointBorderColor: '#fff',
          pointHoverBackgroundColor: '#fff',
          pointHoverBorderColor: colors.line,
          pointRadius: 4,
          pointHoverRadius: 6,
          fill: false,
          tension: 0.25,
          spanGaps: true
        });
      });

      const maxVal = Math.max(...historyData.map(d => d.probability), 0);
      const yMax = Math.min(1.0, Math.max(0.05, Math.ceil(maxVal * 20) / 20));

      if (historyChart) {
        historyChart.data.labels = xLabels;
        historyChart.data.datasets = datasets;
        historyChart.options.scales.y.max = yMax;
        historyChart.update();
      } else {
        historyChart = new Chart(ctx, {
          type: 'line',
          data: {
            labels: xLabels,
            datasets: datasets
          },
          options: {
            responsive: true,
            maintainAspectRatio: false,
            scales: {
              x: {
                grid: {
                  color: 'rgba(255, 255, 255, 0.05)',
                  borderColor: 'rgba(255, 255, 255, 0.1)'
                },
                ticks: {
                  color: '#94a3b8',
                  font: {
                    family: "'Outfit', sans-serif",
                    size: 11
                  }
                }
              },
              y: {
                grid: {
                  color: 'rgba(255, 255, 255, 0.05)',
                  borderColor: 'rgba(255, 255, 255, 0.1)'
                },
                ticks: {
                  color: '#94a3b8',
                  font: {
                    family: "'Outfit', sans-serif",
                    size: 11
                  },
                  callback: function(value) {
                    return (value * 100).toFixed(0) + '%';
                  }
                },
                min: 0,
                max: yMax
              }
            },
            plugins: {
              legend: {
                display: false
              },
              tooltip: {
                backgroundColor: '#0f172a',
                titleColor: '#f8fafc',
                bodyColor: '#cbd5e1',
                borderColor: 'rgba(255, 255, 255, 0.1)',
                borderWidth: 1,
                cornerRadius: 8,
                titleFont: {
                  family: "'Outfit', sans-serif",
                  weight: '600'
                },
                bodyFont: {
                  family: "'Inter', sans-serif"
                },
                callbacks: {
                  label: function(context) {
                    return ` ${context.dataset.label}: ${(context.parsed.y * 100).toFixed(2)}%`;
                  }
                }
              }
            }
          }
        });
      }
    }

    async function rebuildHistoryData() {
      const btn = document.getElementById('btn-rebuild-history');
      const btnText = document.getElementById('rebuild-btn-text');
      const statusDiv = document.getElementById('history-rebuild-status');
      
      btn.disabled = true;
      btnText.innerText = "Running 100,000 simulations...";
      statusDiv.innerText = "Calculating checkpoints, please wait (this may take a few seconds)...";
      statusDiv.style.color = 'var(--text-secondary)';
      
      try {
        const res = await fetch('/api/rebuild-probability-history', { method: 'POST' });
        const data = await res.json();
        
        if (data.ok) {
          statusDiv.innerText = "Rebuild complete!";
          statusDiv.style.color = 'var(--success-color)';
          setTimeout(() => { statusDiv.innerText = ""; statusDiv.style.color = 'var(--text-secondary)'; }, 3000);
          await loadHistoryTab();
        } else {
          statusDiv.innerText = "Rebuild failed: " + data.error;
          statusDiv.style.color = 'var(--danger-color)';
        }
      } catch (e) {
        statusDiv.innerText = "Rebuild request failed.";
        statusDiv.style.color = 'var(--danger-color)';
      } finally {
        btn.disabled = false;
        btnText.innerText = "Rebuild History (100k Sims)";
      }
    }

    // Init page
    switchTab('standings');
  </script>
</body>
</html>
)rawhtml";
}

} // namespace

WebServer::WebServer(Tournament tournament,
                     const std::string& schedulePath,
                     double baseRate,
                     double alpha,
                     double hostAdvantage,
                     int defaultIterations)
    : tournament_(std::move(tournament)),
      schedulePath_(schedulePath),
      baseRate_(baseRate),
      alpha_(alpha),
      hostAdvantage_(hostAdvantage),
      defaultIterations_(defaultIterations) {
    
    tournament_.computeStandings();
    refreshBracketSlotsFromResults();

    std::string historyPath = "data/probability_history.csv";
    std::ifstream f(historyPath.c_str());
    if (!f.good()) {
        std::cout << "Probability history file not found. Building it on startup..." << std::endl;
        rebuildProbabilityHistory(100000);
    } else {
        std::cout << "Using existing probability history file." << std::endl;
    }
}

void WebServer::rebuildProbabilityHistory(int iterations) {
    auto matches = tournament_.allMatches();
    std::sort(matches.begin(), matches.end(), [](const Match& a, const Match& b) {
        return a.matchId() < b.matchId();
    });

    std::vector<Match> completedMatches;
    for (const auto& m : matches) {
        if (m.isFinal()) {
            completedMatches.push_back(m);
        }
    }
    int actualCompleted = completedMatches.size();

    std::string historyPath = "data/probability_history.csv";
    std::ofstream outFile(historyPath);
    if (!outFile.is_open()) {
        std::cerr << "Failed to open probability history file for writing: " << historyPath << std::endl;
        return;
    }

    outFile << "games_played,team,probability\n";

    // 1. Calculate the latest prefix p where each team has played exactly g games
    std::map<std::pair<std::string, int>, int> latestPrefixForTeamAndGames;
    for (const auto& [abbr, _] : tournament_.allTeams()) {
        latestPrefixForTeamAndGames[{abbr, 0}] = 0;
    }
    for (int p = 1; p <= actualCompleted; ++p) {
        for (const auto& [abbr, _] : tournament_.allTeams()) {
            int gp = 0;
            for (int i = 0; i < p; ++i) {
                if (completedMatches[i].homeTeam() == abbr || completedMatches[i].awayTeam() == abbr) {
                    gp++;
                }
            }
            latestPrefixForTeamAndGames[{abbr, gp}] = p;
        }
    }

    // 2. Simulate each prefix and record data points aligned with their latest prefix
    for (int p = 0; p <= actualCompleted; ++p) {
        std::set<int> keepFinalIds;
        for (int i = 0; i < p; ++i) {
            keepFinalIds.insert(completedMatches[i].matchId());
        }

        Tournament simTour = tournament_;
        for (auto& match : simTour.allMatches()) {
            if (keepFinalIds.count(match.matchId()) == 0) {
                match.setScore(-1, -1, -1, -1, "scheduled");
            }
        }
        simTour.computeStandings();

        MonteCarlo mc;
        mc.setModelParameters(baseRate_, alpha_, hostAdvantage_);
        auto results = mc.simulate(simTour, iterations, 12345);

        for (const auto& [abbr, _] : simTour.allTeams()) {
            int gp = 0;
            for (int i = 0; i < p; ++i) {
                if (completedMatches[i].homeTeam() == abbr || completedMatches[i].awayTeam() == abbr) {
                    gp++;
                }
            }

            if (latestPrefixForTeamAndGames.at({abbr, gp}) == p) {
                double prob = results.finalProbability.at(abbr);
                outFile << gp << "," << abbr << "," << std::fixed << std::setprecision(6) << prob << "\n";
            }
        }
    }
    outFile.close();
    std::cout << "Successfully rebuilt probability history up to " << actualCompleted << " completed games using " << iterations << " iterations." << std::endl;
}

WebServer::Response WebServer::handleForTests(const std::string& method,
                                              const std::string& rawPath,
                                              const std::string& body) {
    int statusCode = 200;
    std::string contentType = "text/plain; charset=utf-8";
    const std::string responseBody = handleRequest(method, rawPath, body, statusCode, contentType);
    return {statusCode, contentType, responseBody};
}

void WebServer::run(int port) {
    int serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0) {
        throw std::runtime_error("Failed to create server socket");
    }

    int opt = 1;
    if (setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        close(serverFd);
        throw std::runtime_error("Failed to set socket options");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(serverFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        const std::string err = std::strerror(errno);
        close(serverFd);
        throw std::runtime_error("Failed to bind web server: " + err);
    }

    if (listen(serverFd, 32) < 0) {
        close(serverFd);
        throw std::runtime_error("Failed to listen on web server socket");
    }

    std::cout << "Web server running at http://127.0.0.1:" << port << std::endl;
    std::cout << "Press Ctrl+C to stop." << std::endl;

    while (true) {
        int clientFd = accept(serverFd, nullptr, nullptr);
        if (clientFd < 0) continue;

        try {
            const std::string request = readFullRequest(clientFd);
            std::stringstream reqStream(request);
            std::string requestLine;
            std::getline(reqStream, requestLine);
            if (!requestLine.empty() && requestLine.back() == '\r') {
                requestLine.pop_back();
            }

            std::stringstream lineStream(requestLine);
            std::string method;
            std::string rawPath;
            std::string version;
            lineStream >> method >> rawPath >> version;

            int statusCode = 200;
            std::string contentType = "text/plain; charset=utf-8";
            std::string body;
            const size_t bodyPos = request.find("\r\n\r\n");
            if (bodyPos != std::string::npos) {
                body = request.substr(bodyPos + 4);
            }

            const std::string responseBody = handleRequest(method, rawPath, body, statusCode, contentType);
            const std::string response = buildHttpResponse(statusCode, contentType, responseBody);

            send(clientFd, response.c_str(), response.size(), 0);
        } catch (const std::exception& e) {
            const std::string response = buildHttpResponse(
                500, "text/plain; charset=utf-8",
                std::string("Internal server error: ") + e.what());
            send(clientFd, response.c_str(), response.size(), 0);
        }
        close(clientFd);
    }
}

std::string WebServer::handleRequest(const std::string& method,
                                     const std::string& rawPath,
                                     const std::string& body,
                                     int& statusCode,
                                     std::string& contentType) {
    const std::string path = getPathOnly(rawPath);

    if (method == "GET" && (path == "/" || path == "/standings")) {
        contentType = "text/html; charset=utf-8";
        return buildDashboardHtml();
    }
    if (method == "GET" && path == "/api/standings") {
        contentType = "application/json; charset=utf-8";
        return standingsJson();
    }
    if (method == "GET" && path == "/api/games") {
        contentType = "application/json; charset=utf-8";
        std::ostringstream out;
        out << "{\"games\":[";
        bool first = true;
        for (const auto& match : tournament_.allMatches()) {
            if (!first) out << ',';
            first = false;

            double homeWinProb = -1.0;
            double awayWinProb = -1.0;
            if (match.stage() != "group") {
                const Team* homeTeam = tournament_.getTeam(match.homeTeam());
                const Team* awayTeam = tournament_.getTeam(match.awayTeam());
                if (homeTeam && awayTeam) {
                    homeWinProb = calculateMatchWinProbability(*homeTeam, *awayTeam);
                    awayWinProb = 1.0 - homeWinProb;
                }
            }

            out << "{\"match_id\":" << match.matchId()
                << ",\"stage\":\"" << jsonEscape(match.stage())
                << "\",\"group\":\"" << jsonEscape(match.group())
                << "\",\"date\":\"" << jsonEscape(match.date())
                << "\",\"home_team\":\"" << jsonEscape(match.homeTeam())
                << "\",\"away_team\":\"" << jsonEscape(match.awayTeam())
                << "\",\"home_score\":" << match.homeScore()
                << ",\"away_score\":" << match.awayScore();
            if (homeWinProb >= 0.0) {
                out << ",\"home_win_prob\":" << homeWinProb
                    << ",\"away_win_prob\":" << awayWinProb;
            }
            out << ",\"status\":\"" << jsonEscape(match.status())
                << "\",\"host_city\":\"" << jsonEscape(match.hostCity()) << "\"}";
        }
        out << "]}";
        return out.str();
    }
    if (path == "/api/simulation") {
        contentType = "application/json; charset=utf-8";
        int iterations = defaultIterations_;
        std::string locksStr = "";

        if (method == "POST") {
            const auto params = parseUrlEncoded(body);
            auto it = params.find("iterations");
            if (it != params.end()) {
                try { iterations = std::stoi(it->second); } catch (...) {}
            }
            it = params.find("locks");
            if (it != params.end()) locksStr = it->second;
        } else {
            iterations = parseIterations(rawPath, defaultIterations_);
            const size_t queryPos = rawPath.find('?');
            if (queryPos != std::string::npos) {
                const std::string query = rawPath.substr(queryPos + 1);
                std::stringstream ss(query);
                std::string part;
                while (std::getline(ss, part, '&')) {
                    const size_t eq = part.find('=');
                    if (eq != std::string::npos) {
                        const std::string key = part.substr(0, eq);
                        const std::string value = part.substr(eq + 1);
                        if (key == "locks") locksStr = urlDecodeLocal(value);
                    }
                }
            }
        }
        return simulationJson(iterations, locksStr);
    }
    if (method == "GET" && path == "/api/impact") {
        contentType = "application/json; charset=utf-8";
        return impactJson(parseIterations(rawPath, defaultIterations_));
    }
    if (method == "GET" && getPathOnly(rawPath) == "/api/sync-scores") {
        contentType = "application/json; charset=utf-8";
        std::string date = parseQueryParam(rawPath, "date");
        
        // Sanitize date to prevent command injection
        std::string sanitizedDate;
        for (char c : date) {
            if (std::isalnum(c) || c == '-') {
                sanitizedDate += c;
            }
        }
        
        std::string cmd = "python3 scripts/fetch_live_scores.py --schedule " + schedulePath_;
        if (!sanitizedDate.empty()) {
            cmd += " --date " + sanitizedDate;
        }
        
        // Execute python score ingestion script
        int status = std::system(cmd.c_str());
        if (status != 0) {
            statusCode = 500;
            return "{\"ok\":false,\"error\":\"Python score ingestion script exited with code " + std::to_string(status) + "\"}";
        }
        
        // Reload tournament in memory to pick up disk changes
        try {
            tournament_ = wc::loadTournamentFromCsvFiles("data/teams.csv", schedulePath_);
          tournament_.computeStandings();
          refreshBracketSlotsFromResults();
          if (!persistSchedule()) {
            statusCode = 500;
            return "{\"ok\":false,\"error\":\"Failed to persist schedule after slot resolution\"}";
          }
        } catch (const std::exception& e) {
            statusCode = 500;
            return "{\"ok\":false,\"error\":\"Failed to reload tournament schedule: " + std::string(e.what()) + "\"}";
        }
        
        return "{\"ok\":true}";
    }
    if (path == "/api/update-result") {
        if (method != "POST") {
            statusCode = 405;
            return "{\"error\":\"POST required\"}";
        }
        std::string error;
        if (!applyResultUpdate(body, error)) {
            statusCode = 400;
            return "{\"ok\":false,\"error\":\"" + jsonEscape(error) + "\"}";
        }
        return "{\"ok\":true}";
    }
    if (method == "GET" && path == "/api/probability-history") {
        contentType = "application/json; charset=utf-8";
        try {
            auto table = CsvParser::parse("data/probability_history.csv");
            std::ostringstream out;
            out << "[";
            bool first = true;
            for (const auto& row : table) {
                if (!first) out << ",";
                first = false;
                out << "{\"games_played\":" << row.at("games_played")
                    << ",\"team\":\"" << jsonEscape(row.at("team")) << "\""
                    << ",\"probability\":" << row.at("probability") << "}";
            }
            out << "]";
            return out.str();
        } catch (const std::exception& e) {
            statusCode = 500;
            return "{\"error\":\"Failed to load probability history: " + std::string(e.what()) + "\"}";
        }
    }
    if (path == "/api/rebuild-probability-history") {
        if (method != "POST") {
            statusCode = 405;
            return "{\"error\":\"POST required\"}";
        }
        contentType = "application/json; charset=utf-8";
        try {
            rebuildProbabilityHistory(100000);
            return "{\"ok\":true}";
        } catch (const std::exception& e) {
            statusCode = 500;
            return "{\"ok\":false,\"error\":\"Failed to rebuild probability history: " + std::string(e.what()) + "\"}";
        }
    }

    statusCode = 404;
    return "Not found";
}

double WebServer::calculateMatchWinProbability(const Team& home, const Team& away) const {
    double eloDiff = home.eloRating() - away.eloRating();
    double homeBoost = (home.abbreviation() == "USA" || home.abbreviation() == "MEX" || home.abbreviation() == "CAN") ? hostAdvantage_ : 0.0;
    double awayBoost = (away.abbreviation() == "USA" || away.abbreviation() == "MEX" || away.abbreviation() == "CAN") ? hostAdvantage_ : 0.0;

    double lambdaHome = baseRate_ * std::exp(alpha_ * eloDiff + homeBoost);
    double lambdaAway = baseRate_ * std::exp(-alpha_ * eloDiff + awayBoost);

    std::mt19937 rng(12345); 
    std::poisson_distribution<int> homeDist(lambdaHome);
    std::poisson_distribution<int> awayDist(lambdaAway);
    std::poisson_distribution<int> homeETDist(lambdaHome / 3.0);
    std::poisson_distribution<int> awayETDist(lambdaAway / 3.0);
    std::uniform_real_distribution<> coin(0.0, 1.0);

    int homeWins = 0;
    const int runs = 10000;
    for (int i = 0; i < runs; ++i) {
        int hs = homeDist(rng);
        int as = awayDist(rng);
        if (hs > as) {
            homeWins++;
        } else if (hs < as) {
            // away wins
        } else {
            hs += homeETDist(rng);
            as += awayETDist(rng);
            if (hs > as) {
                homeWins++;
            } else if (hs < as) {
                // away wins
            } else {
                if (coin(rng) < 0.5) {
                    homeWins++;
                }
            }
        }
    }
    return static_cast<double>(homeWins) / runs;
}

std::string WebServer::standingsJson() const {
    std::ostringstream out;
    out << "{\"groups\":{";

    // Re-evaluate group standings
    Tournament temp = tournament_;
    temp.computeStandings();

    MonteCarlo mc;
    mc.setModelParameters(baseRate_, alpha_, hostAdvantage_);
    auto results = mc.simulate(temp, 100000, 12345);

    std::vector<std::string> groupsList = temp.getGroups();
    std::sort(groupsList.begin(), groupsList.end());

    // Identify third-place qualifiers
    std::vector<Team*> thirdPlaces;
    for (const auto& group : groupsList) {
        auto standing = temp.teamsByGroup(group);
        if (standing.size() >= 3) {
            thirdPlaces.push_back(standing[2]);
        }
    }
    auto rankedThirds = Tiebreaker::rankThirdPlaces(thirdPlaces);
    std::set<std::string> bestThirdsAbbr;
    for (size_t i = 0; i < 8 && i < rankedThirds.size(); ++i) {
        bestThirdsAbbr.insert(rankedThirds[i]->abbreviation());
    }

    bool firstGroup = true;
    for (const auto& group : groupsList) {
        if (!firstGroup) out << ',';
        firstGroup = false;

        out << "\"" << group << "\":[";
        auto standings = temp.teamsByGroup(group);
        bool firstTeam = true;
        for (const auto* team : standings) {
            if (!firstTeam) out << ',';
            firstTeam = false;

            bool adv3rd = bestThirdsAbbr.count(team->abbreviation()) > 0;
            std::string clinchedStatus = "";
            auto r32it = results.r32Probability.find(team->abbreviation());
            if (r32it != results.r32Probability.end()) {
                double r32 = r32it->second;
                double g1 = results.group1stProbability.at(team->abbreviation());
                double g2 = results.group2ndProbability.at(team->abbreviation());
                double g3 = results.group3rdProbability.at(team->abbreviation());

                if (g1 >= 0.999999) {
                    clinchedStatus = "CLINCHED 1ST";
                } else if (g2 >= 0.999999) {
                    clinchedStatus = "CLINCHED 2ND";
                } else if (g3 >= 0.999999) {
                    clinchedStatus = "CLINCHED 3RD";
                } else if (g1 + g2 >= 0.999999) {
                    clinchedStatus = "CLINCHED TOP 2";
                } else if (r32 >= 0.999999) {
                    clinchedStatus = "CLINCHED R32";
                } else if (r32 <= 0.000001) {
                    clinchedStatus = "ELIMINATED";
                }
            }

            out << "{\"abbr\":\"" << team->abbreviation()
                << "\",\"name\":\"" << jsonEscape(team->fullName())
                << "\",\"pts\":" << team->points()
                << ",\"gd\":" << team->goalDifference()
                << ",\"gf\":" << team->goalsFor()
                << ",\"adv_3rd\":" << (adv3rd ? "true" : "false")
                << ",\"clinched_status\":\"" << clinchedStatus << "\"}";
        }
        out << "]";
    }

    out << "}}";
    return out.str();
}

std::string WebServer::simulationJson(int iterations, const std::string& locksStr) const {
    Tournament simTour = tournament_;

    // Apply outcome locks for Sandbox scenarios
    // Format: match_id:winner (winner = "home", "draw", "away")
    if (!locksStr.empty()) {
        std::stringstream ss(locksStr);
        std::string part;
        while (std::getline(ss, part, ',')) {
            const size_t colon = part.find(':');
            if (colon == std::string::npos) continue;
            int matchId = std::stoi(part.substr(0, colon));
            std::string outcome = part.substr(colon + 1);

            for (auto& match : simTour.allMatches()) {
                if (match.matchId() == matchId) {
                    if (outcome == "home") match.setScore(2, 0, -1, -1, "final");
                    else if (outcome == "draw") match.setScore(1, 1, -1, -1, "final");
                    else if (outcome == "away") match.setScore(0, 2, -1, -1, "final");
                    break;
                }
            }
        }
    }

    simTour.computeStandings();

    MonteCarlo mc;
    mc.setModelParameters(baseRate_, alpha_, hostAdvantage_);
    auto results = mc.simulate(simTour, iterations, 12345);

    std::ostringstream out;
    out << "{\"r32\":{";
    bool first = true;
    for (const auto& [abbr, p] : results.r32Probability) {
        if (!first) out << ',';
        first = false;
        out << "\"" << abbr << "\":" << p;
    }
    out << "},\"r16\":{";
    first = true;
    for (const auto& [abbr, p] : results.r16Probability) {
        if (!first) out << ',';
        first = false;
        out << "\"" << abbr << "\":" << p;
    }
    out << "},\"qf\":{";
    first = true;
    for (const auto& [abbr, p] : results.qfProbability) {
        if (!first) out << ',';
        first = false;
        out << "\"" << abbr << "\":" << p;
    }
    out << "},\"sf\":{";
    first = true;
    for (const auto& [abbr, p] : results.sfProbability) {
        if (!first) out << ',';
        first = false;
        out << "\"" << abbr << "\":" << p;
    }
    out << "},\"final\":{";
    first = true;
    for (const auto& [abbr, p] : results.finalProbability) {
        if (!first) out << ',';
        first = false;
        out << "\"" << abbr << "\":" << p;
    }
    out << "},\"champion\":{";
    first = true;
    for (const auto& [abbr, p] : results.championProbability) {
        if (!first) out << ',';
        first = false;
        out << "\"" << abbr << "\":" << p;
    }
    out << "}}";

    return out.str();
}

std::string WebServer::impactJson(int iterations) const {
    Tournament temp = tournament_;
    temp.computeStandings();

    MonteCarlo mc;
    mc.setModelParameters(baseRate_, alpha_, hostAdvantage_);
    auto analysis = mc.analyzeImpact(temp, iterations, 12345);

    std::ostringstream out;
    out << "{\"impacts\":[";
    bool first = true;
    for (const auto& imp : analysis.gameImpacts) {
        if (!first) out << ',';
        first = false;
        out << "{\"match_id\":" << imp.matchId
            << ",\"date\":\"" << jsonEscape(imp.date) << "\""
            << ",\"home_team\":\"" << jsonEscape(imp.homeTeam) << "\""
            << ",\"away_team\":\"" << jsonEscape(imp.awayTeam) << "\""
            << ",\"home_delta\":" << imp.homeDeltaR32
            << ",\"away_delta\":" << imp.awayDeltaR32 << "}";
    }
    out << "]}";

    return out.str();
}

bool WebServer::applyResultUpdate(const std::string& body, std::string& error) {
    const auto params = parseUrlEncoded(body);
    auto itId = params.find("match_id");
    auto itHome = params.find("home_score");
    auto itAway = params.find("away_score");
    auto itHomePen = params.find("home_penalty_score");
    auto itAwayPen = params.find("away_penalty_score");

    if (itId == params.end() || itHome == params.end() || itAway == params.end()) {
        error = "Missing match_id, home_score, or away_score";
        return false;
    }

    try {
        int matchId = std::stoi(itId->second);
        int homeScore = std::stoi(itHome->second);
        int awayScore = std::stoi(itAway->second);
        int homePen = -1;
        int awayPen = -1;
        
        if (itHomePen != params.end() && !itHomePen->second.empty()) {
            homePen = std::stoi(itHomePen->second);
        }
        if (itAwayPen != params.end() && !itAwayPen->second.empty()) {
            awayPen = std::stoi(itAwayPen->second);
        }
        
        if ((homePen != -1 || awayPen != -1) && homeScore != awayScore) {
            error = "Penalty scores can only be applied when the regular score is tied.";
            return false;
        }

        bool found = false;
        for (auto& match : tournament_.allMatches()) {
            if (match.matchId() == matchId) {
                bool isKnockout = match.stage() != "group";
                if (isKnockout && homeScore == awayScore && (homePen == -1 || awayPen == -1 || homePen == awayPen)) {
                    error = "Elimination round matches ending in a tie require valid, non-tied penalty scores.";
                    return false;
                }
                
                match.setScore(homeScore, awayScore, homePen, awayPen, "final");
                found = true;
                break;
            }
        }

        if (!found) {
            error = "Match ID " + std::to_string(matchId) + " not found";
            return false;
        }

        tournament_.computeStandings();
        refreshBracketSlotsFromResults();
        return persistSchedule();
    } catch (const std::exception& e) {
        error = std::string("Parsing error: ") + e.what();
        return false;
    }
}

    void WebServer::refreshBracketSlotsFromResults() {
      const std::vector<std::string> groups = {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L"};
      std::map<std::string, bool> groupFinalized;
      std::map<std::string, std::vector<Team*>> groupStandings;

      bool allGroupsFinalized = true;
      for (const auto& group : groups) {
        const bool finalized = isGroupFinalized(tournament_, group);
        groupFinalized[group] = finalized;
        allGroupsFinalized = allGroupsFinalized && finalized;
        groupStandings[group] = tournament_.teamsByGroup(group);
      }

      MonteCarlo mc;
      mc.setModelParameters(baseRate_, alpha_, hostAdvantage_);
      auto results = mc.simulate(tournament_, 10000, 12345);

      auto resolveGroupSlot = [&](const std::string& slot) -> std::string {
        if (startsWith(slot, "WINNER_GROUP_")) {
          const std::string group = slot.substr(std::string("WINNER_GROUP_").size());
          if (groupStandings.find(group) != groupStandings.end()) {
            for (const auto* team : groupStandings[group]) {
              auto it = results.group1stProbability.find(team->abbreviation());
              if (it != results.group1stProbability.end() && it->second >= 0.999999) {
                return team->abbreviation();
              }
            }
          }
          if (groupFinalized[group] && groupStandings[group].size() >= 1) {
            return groupStandings[group][0]->abbreviation();
          }
        }
        if (startsWith(slot, "RUNNER_UP_GROUP_")) {
          const std::string group = slot.substr(std::string("RUNNER_UP_GROUP_").size());
          if (groupStandings.find(group) != groupStandings.end()) {
            for (const auto* team : groupStandings[group]) {
              auto it = results.group2ndProbability.find(team->abbreviation());
              if (it != results.group2ndProbability.end() && it->second >= 0.999999) {
                return team->abbreviation();
              }
            }
          }
          if (groupFinalized[group] && groupStandings[group].size() >= 2) {
            return groupStandings[group][1]->abbreviation();
          }
        }
        return "";
      };

      // Resolve only guaranteed winner/runner-up slots as soon as each group is finalized.
      // Third-place dependent slots are only resolved after all groups are finalized.
      for (auto& match : tournament_.allMatches()) {
        if (match.stage() != "knockout_r32" || match.isFinal()) {
          continue;
        }

        std::string home = match.homeTeam();
        std::string away = match.awayTeam();

        if (!allGroupsFinalized) {
          const std::string resolvedHome = resolveGroupSlot(home);
          const std::string resolvedAway = resolveGroupSlot(away);
          if (!resolvedHome.empty()) home = resolvedHome;
          if (!resolvedAway.empty()) away = resolvedAway;
          if (home != match.homeTeam() || away != match.awayTeam()) {
            match.setTeams(home, away);
          }
        }
      }

      if (allGroupsFinalized) {
        Tiebreaker::allocateRoundOf32Matchups(tournament_);
      }

      auto resolveWinnerReference = [&](const std::string& slot) -> std::string {
        int refId = -1;
        if (startsWith(slot, "R32_WINNER_")) {
          if (parseInt(slot.substr(std::string("R32_WINNER_").size()), refId)) {
            return winnerOfFinalMatch(findMatchById(tournament_, refId));
          }
        }
        if (startsWith(slot, "R16_WINNER_")) {
          if (parseInt(slot.substr(std::string("R16_WINNER_").size()), refId)) {
            return winnerOfFinalMatch(findMatchById(tournament_, refId));
          }
        }
        if (startsWith(slot, "QUARTERFINAL_WINNER_")) {
          if (parseInt(slot.substr(std::string("QUARTERFINAL_WINNER_").size()), refId)) {
            return winnerOfFinalMatch(findMatchById(tournament_, refId));
          }
        }
        if (startsWith(slot, "SEMIFINAL_WINNER_")) {
          if (parseInt(slot.substr(std::string("SEMIFINAL_WINNER_").size()), refId)) {
            return winnerOfFinalMatch(findMatchById(tournament_, refId));
          }
        }
        return "";
      };

      // Propagate only finalized match winners to downstream rounds.
      for (auto& match : tournament_.allMatches()) {
        if (match.isFinal()) {
          continue;
        }
        std::string home = match.homeTeam();
        std::string away = match.awayTeam();

        const std::string resolvedHome = resolveWinnerReference(home);
        const std::string resolvedAway = resolveWinnerReference(away);

        if (!resolvedHome.empty()) home = resolvedHome;
        if (!resolvedAway.empty()) away = resolvedAway;

        if (home != match.homeTeam() || away != match.awayTeam()) {
          match.setTeams(home, away);
        }
      }
    }

bool WebServer::persistSchedule() const {
    try {
        CsvParser::Table table;
        for (const auto& match : tournament_.allMatches()) {
            table.push_back({
                {"match_id", std::to_string(match.matchId())},
                {"stage", match.stage()},
                {"group", match.group()},
                {"date", match.date()},
                {"home_team", match.homeTeam()},
                {"away_team", match.awayTeam()},
                {"home_score", std::to_string(match.homeScore())},
                {"away_score", std::to_string(match.awayScore())},
                {"home_penalty_score", std::to_string(match.homePenaltyScore())},
                {"away_penalty_score", std::to_string(match.awayPenaltyScore())},
                {"status", match.status()},
                {"host_city", match.hostCity()}
            });
        }
        CsvParser::write(schedulePath_, {
            "match_id", "stage", "group", "date", "home_team", "away_team",
            "home_score", "away_score", "home_penalty_score", "away_penalty_score", "status", "host_city"
        }, table);
        return true;
    } catch (...) {
        return false;
    }
}

std::string WebServer::jsonEscape(const std::string& value) {
    std::string out;
    for (char c : value) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else out += c;
    }
    return out;
}

std::string WebServer::getPathOnly(const std::string& rawPath) {
    const size_t q = rawPath.find('?');
    if (q == std::string::npos) return rawPath;
    return rawPath.substr(0, q);
}

int WebServer::parseIterations(const std::string& rawPath, int fallback) {
    const size_t q = rawPath.find('?');
    if (q == std::string::npos) return fallback;
    const std::string query = rawPath.substr(q + 1);
    std::stringstream ss(query);
    std::string part;
    while (std::getline(ss, part, '&')) {
        const size_t eq = part.find('=');
        if (eq != std::string::npos) {
            const std::string key = part.substr(0, eq);
            const std::string val = part.substr(eq + 1);
            if (key == "iterations") {
                try { return std::stoi(val); } catch (...) {}
            }
        }
    }
    return fallback;
}

std::string WebServer::parseQueryParam(const std::string& rawPath, const std::string& key) {
    const size_t q = rawPath.find('?');
    if (q == std::string::npos) return "";
    const std::string query = rawPath.substr(q + 1);
    std::stringstream ss(query);
    std::string part;
    while (std::getline(ss, part, '&')) {
        const size_t eq = part.find('=');
        if (eq != std::string::npos) {
            const std::string k = part.substr(0, eq);
            const std::string v = part.substr(eq + 1);
            if (k == key) {
                return urlDecodeLocal(v);
            }
        }
    }
    return "";
}

std::string WebServer::buildHttpResponse(int statusCode,
                                         const std::string& contentType,
                                         const std::string& body) {
    std::ostringstream out;
    out << "HTTP/1.1 " << statusCode << " " << statusText(statusCode) << "\r\n"
        << "Content-Type: " << contentType << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n"
        << "Access-Control-Allow-Origin: *\r\n"
        << "\r\n"
        << body;
    return out.str();
}
