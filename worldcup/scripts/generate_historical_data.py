#!/usr/bin/env python3
import csv
from pathlib import Path

# Fields: year,stage,home_team,away_team,home_score,away_score,home_elo,away_elo,is_host_home,is_host_away
matches = [
    # 2022 World Cup
    {"year": 2022, "stage": "group", "home_team": "QAT", "away_team": "ECU", "home_score": 0, "away_score": 2, "home_elo": 1520, "away_elo": 1640, "is_host_home": 1, "is_host_away": 0},
    {"year": 2022, "stage": "group", "home_team": "SEN", "away_team": "NED", "home_score": 0, "away_score": 2, "home_elo": 1650, "away_elo": 1820, "is_host_home": 0, "is_host_away": 0},
    {"year": 2022, "stage": "group", "home_team": "ENG", "away_team": "IRN", "home_score": 6, "away_score": 2, "home_elo": 1840, "away_elo": 1580, "is_host_home": 0, "is_host_away": 0},
    {"year": 2022, "stage": "group", "home_team": "USA", "away_team": "WAL", "home_score": 1, "away_score": 1, "home_elo": 1660, "away_elo": 1610, "is_host_home": 0, "is_host_away": 0},
    {"year": 2022, "stage": "group", "home_team": "ARG", "away_team": "KSA", "home_score": 1, "away_score": 2, "home_elo": 1920, "away_elo": 1450, "is_host_home": 0, "is_host_away": 0},
    {"year": 2022, "stage": "group", "home_team": "MEX", "away_team": "POL", "home_score": 0, "away_score": 0, "home_elo": 1680, "away_elo": 1630, "is_host_home": 0, "is_host_away": 0},
    {"year": 2022, "stage": "group", "home_team": "FRA", "away_team": "AUS", "home_score": 4, "away_score": 1, "home_elo": 1880, "away_elo": 1550, "is_host_home": 0, "is_host_away": 0},
    {"year": 2022, "stage": "group", "home_team": "GER", "away_team": "JPN", "home_score": 1, "away_score": 2, "home_elo": 1780, "away_elo": 1610, "is_host_home": 0, "is_host_away": 0},
    {"year": 2022, "stage": "group", "home_team": "ESP", "away_team": "CRC", "home_score": 7, "away_score": 0, "home_elo": 1820, "away_elo": 1520, "is_host_home": 0, "is_host_away": 0},
    {"year": 2022, "stage": "group", "home_team": "BEL", "away_team": "CAN", "home_score": 1, "away_score": 0, "home_elo": 1830, "away_elo": 1590, "is_host_home": 0, "is_host_away": 0},
    {"year": 2022, "stage": "group", "home_team": "BRA", "away_team": "SRB", "home_score": 2, "away_score": 0, "home_elo": 1950, "away_elo": 1660, "is_host_home": 0, "is_host_away": 0},
    {"year": 2022, "stage": "group", "home_team": "POR", "away_team": "GHA", "home_score": 3, "away_score": 2, "home_elo": 1800, "away_elo": 1480, "is_host_home": 0, "is_host_away": 0},
    {"year": 2022, "stage": "group", "home_team": "QAT", "away_team": "SEN", "home_score": 1, "away_score": 3, "home_elo": 1500, "away_elo": 1640, "is_host_home": 1, "is_host_away": 0},
    {"year": 2022, "stage": "group", "home_team": "NED", "away_team": "ECU", "home_score": 1, "away_score": 1, "home_elo": 1830, "away_elo": 1650, "is_host_home": 0, "is_host_away": 0},
    {"year": 2022, "stage": "group", "home_team": "ENG", "away_team": "USA", "home_score": 0, "away_score": 0, "home_elo": 1860, "away_elo": 1665, "is_host_home": 0, "is_host_away": 0},
    {"year": 2022, "stage": "group", "home_team": "ARG", "away_team": "MEX", "home_score": 2, "away_score": 0, "home_elo": 1900, "away_elo": 1680, "is_host_home": 0, "is_host_away": 0},
    {"year": 2022, "stage": "group", "home_team": "ESP", "away_team": "GER", "home_score": 1, "away_score": 1, "home_elo": 1840, "away_elo": 1765, "is_host_home": 0, "is_host_away": 0},
    {"year": 2022, "stage": "group", "home_team": "POR", "away_team": "URU", "home_score": 2, "away_score": 0, "home_elo": 1810, "away_elo": 1720, "is_host_home": 0, "is_host_away": 0},
    {"year": 2022, "stage": "group", "home_team": "NED", "away_team": "QAT", "home_score": 2, "away_score": 0, "home_elo": 1835, "away_elo": 1490, "is_host_home": 0, "is_host_away": 1},
    {"year": 2022, "stage": "group", "home_team": "POL", "away_team": "ARG", "home_score": 0, "away_score": 2, "home_elo": 1640, "away_elo": 1910, "is_host_home": 0, "is_host_away": 0},
    {"year": 2022, "stage": "group", "home_team": "CRO", "away_team": "BEL", "home_score": 0, "away_score": 0, "home_elo": 1730, "away_elo": 1820, "is_host_home": 0, "is_host_away": 0},
    {"year": 2022, "stage": "group", "home_team": "CMR", "away_team": "BRA", "home_score": 1, "away_score": 0, "home_elo": 1540, "away_elo": 1960, "is_host_home": 0, "is_host_away": 0},
    {"year": 2022, "stage": "knockout", "home_team": "NED", "away_team": "USA", "home_score": 3, "away_score": 1, "home_elo": 1840, "away_elo": 1670, "is_host_home": 0, "is_host_away": 0},
    {"year": 2022, "stage": "knockout", "home_team": "ARG", "away_team": "AUS", "home_score": 2, "away_score": 1, "home_elo": 1920, "away_elo": 1580, "is_host_home": 0, "is_host_away": 0},
    {"year": 2022, "stage": "knockout", "home_team": "FRA", "away_team": "POL", "home_score": 3, "away_score": 1, "home_elo": 1890, "away_elo": 1630, "is_host_home": 0, "is_host_away": 0},
    {"year": 2022, "stage": "knockout", "home_team": "ENG", "away_team": "SEN", "home_score": 3, "away_score": 0, "home_elo": 1865, "away_elo": 1650, "is_host_home": 0, "is_host_away": 0},
    {"year": 2022, "stage": "knockout", "home_team": "BRA", "away_team": "KOR", "home_score": 4, "away_score": 1, "home_elo": 1955, "away_elo": 1590, "is_host_home": 0, "is_host_away": 0},
    {"year": 2022, "stage": "knockout", "home_team": "MAR", "away_team": "ESP", "home_score": 0, "away_score": 0, "home_elo": 1690, "away_elo": 1835, "is_host_home": 0, "is_host_away": 0},
    {"year": 2022, "stage": "knockout", "home_team": "HRV", "away_team": "BRA", "home_score": 1, "away_score": 1, "home_elo": 1740, "away_elo": 1965, "is_host_home": 0, "is_host_away": 0},
    {"year": 2022, "stage": "knockout", "home_team": "NED", "away_team": "ARG", "home_score": 2, "away_score": 2, "home_elo": 1850, "away_elo": 1925, "is_host_home": 0, "is_host_away": 0},
    {"year": 2022, "stage": "knockout", "home_team": "MAR", "away_team": "POR", "home_score": 1, "away_score": 0, "home_elo": 1720, "away_elo": 1820, "is_host_home": 0, "is_host_away": 0},
    {"year": 2022, "stage": "knockout", "home_team": "ENG", "away_team": "FRA", "home_score": 1, "away_score": 2, "home_elo": 1875, "away_elo": 1900, "is_host_home": 0, "is_host_away": 0},
    {"year": 2022, "stage": "knockout", "home_team": "ARG", "away_team": "HRV", "home_score": 3, "away_score": 0, "home_elo": 1930, "away_elo": 1750, "is_host_home": 0, "is_host_away": 0},
    {"year": 2022, "stage": "knockout", "home_team": "FRA", "away_team": "MAR", "home_score": 2, "away_score": 0, "home_elo": 1910, "away_elo": 1730, "is_host_home": 0, "is_host_away": 0},
    {"year": 2022, "stage": "knockout", "home_team": "HRV", "away_team": "MAR", "home_score": 2, "away_score": 1, "home_elo": 1740, "away_elo": 1720, "is_host_home": 0, "is_host_away": 0},
    {"year": 2022, "stage": "knockout", "home_team": "ARG", "away_team": "FRA", "home_score": 3, "away_score": 3, "home_elo": 1945, "away_elo": 1920, "is_host_home": 0, "is_host_away": 0},

    # 2018 World Cup
    {"year": 2018, "stage": "group", "home_team": "RUS", "away_team": "KSA", "home_score": 5, "away_score": 0, "home_elo": 1580, "away_elo": 1410, "is_host_home": 1, "is_host_away": 0},
    {"year": 2018, "stage": "group", "home_team": "POR", "away_team": "ESP", "home_score": 3, "away_score": 3, "home_elo": 1820, "away_elo": 1860, "is_host_home": 0, "is_host_away": 0},
    {"year": 2018, "stage": "group", "home_team": "FRA", "away_team": "AUS", "home_score": 2, "away_score": 1, "home_elo": 1840, "away_elo": 1560, "is_host_home": 0, "is_host_away": 0},
    {"year": 2018, "stage": "group", "home_team": "ARG", "away_team": "ISL", "home_score": 1, "away_score": 1, "home_elo": 1880, "away_elo": 1590, "is_host_home": 0, "is_host_away": 0},
    {"year": 2018, "stage": "group", "home_team": "GER", "away_team": "MEX", "home_score": 0, "away_score": 1, "home_elo": 1910, "away_elo": 1700, "is_host_home": 0, "is_host_away": 0},
    {"year": 2018, "stage": "group", "home_team": "BRA", "away_team": "SUI", "home_score": 1, "away_score": 1, "home_elo": 1930, "away_elo": 1730, "is_host_home": 0, "is_host_away": 0},
    {"year": 2018, "stage": "group", "home_team": "RUS", "away_team": "EGY", "home_score": 3, "away_score": 1, "home_elo": 1600, "away_elo": 1530, "is_host_home": 1, "is_host_away": 0},
    {"year": 2018, "stage": "group", "home_team": "ARG", "away_team": "HRV", "home_score": 0, "away_score": 3, "home_elo": 1870, "away_elo": 1720, "is_host_home": 0, "is_host_away": 0},
    {"year": 2018, "stage": "group", "home_team": "ENG", "away_team": "PAN", "home_score": 6, "away_score": 1, "home_elo": 1760, "away_elo": 1460, "is_host_home": 0, "is_host_away": 0},
    {"year": 2018, "stage": "group", "home_team": "URU", "away_team": "RUS", "home_score": 3, "away_score": 0, "home_elo": 1740, "away_elo": 1630, "is_host_home": 0, "is_host_away": 1},
    {"year": 2018, "stage": "group", "home_team": "KOR", "away_team": "GER", "home_score": 2, "away_score": 0, "home_elo": 1540, "away_elo": 1890, "is_host_home": 0, "is_host_away": 0},
    {"year": 2018, "stage": "knockout", "home_team": "FRA", "away_team": "ARG", "home_score": 4, "away_score": 3, "home_elo": 1860, "away_elo": 1840, "is_host_home": 0, "is_host_away": 0},
    {"year": 2018, "stage": "knockout", "home_team": "URU", "away_team": "POR", "home_score": 2, "away_score": 1, "home_elo": 1750, "away_elo": 1810, "is_host_home": 0, "is_host_away": 0},
    {"year": 2018, "stage": "knockout", "home_team": "RUS", "away_team": "ESP", "home_score": 1, "away_score": 1, "home_elo": 1610, "away_elo": 1850, "is_host_home": 1, "is_host_away": 0},
    {"year": 2018, "stage": "knockout", "home_team": "HRV", "away_team": "DEN", "home_score": 1, "away_score": 1, "home_elo": 1760, "away_elo": 1720, "is_host_home": 0, "is_host_away": 0},
    {"year": 2018, "stage": "knockout", "home_team": "FRA", "away_team": "URU", "home_score": 2, "away_score": 0, "home_elo": 1880, "away_elo": 1770, "is_host_home": 0, "is_host_away": 0},
    {"year": 2018, "stage": "knockout", "home_team": "BRA", "away_team": "BEL", "home_score": 1, "away_score": 2, "home_elo": 1940, "away_elo": 1850, "is_host_home": 0, "is_host_away": 0},
    {"year": 2018, "stage": "knockout", "home_team": "HRV", "away_team": "RUS", "home_score": 2, "away_score": 2, "home_elo": 1770, "away_elo": 1620, "is_host_home": 0, "is_host_away": 1},
    {"year": 2018, "stage": "knockout", "home_team": "FRA", "away_team": "BEL", "home_score": 1, "away_score": 0, "home_elo": 1900, "away_elo": 1870, "is_host_home": 0, "is_host_away": 0},
    {"year": 2018, "stage": "knockout", "home_team": "HRV", "away_team": "ENG", "home_score": 2, "away_score": 1, "home_elo": 1780, "away_elo": 1800, "is_host_home": 0, "is_host_away": 0},
    {"year": 2018, "stage": "knockout", "home_team": "FRA", "away_team": "HRV", "home_score": 4, "away_score": 2, "home_elo": 1910, "away_elo": 1810, "is_host_home": 0, "is_host_away": 0},

    # 2014 World Cup
    {"year": 2014, "stage": "group", "home_team": "BRA", "away_team": "HRV", "home_score": 3, "away_score": 1, "home_elo": 1910, "away_elo": 1680, "is_host_home": 1, "is_host_away": 0},
    {"year": 2014, "stage": "group", "home_team": "ESP", "away_team": "NED", "home_score": 1, "away_score": 5, "home_elo": 1930, "away_elo": 1810, "is_host_home": 0, "is_host_away": 0},
    {"year": 2014, "stage": "group", "home_team": "ENG", "away_team": "ITA", "home_score": 1, "away_score": 2, "home_elo": 1740, "away_elo": 1790, "is_host_home": 0, "is_host_away": 0},
    {"year": 2014, "stage": "group", "home_team": "GER", "away_team": "POR", "home_score": 4, "away_score": 0, "home_elo": 1900, "away_elo": 1780, "is_host_home": 0, "is_host_away": 0},
    {"year": 2014, "stage": "group", "home_team": "BRA", "away_team": "MEX", "home_score": 0, "away_score": 0, "home_elo": 1920, "away_elo": 1690, "is_host_home": 1, "is_host_away": 0},
    {"year": 2014, "stage": "group", "home_team": "CRC", "away_team": "ITA", "home_score": 1, "away_score": 0, "home_elo": 1590, "away_elo": 1800, "is_host_home": 0, "is_host_away": 0},
    {"year": 2014, "stage": "group", "home_team": "CMR", "away_team": "BRA", "home_score": 1, "away_score": 4, "home_elo": 1490, "away_elo": 1920, "is_host_home": 0, "is_host_away": 1},
    {"year": 2014, "stage": "knockout", "home_team": "BRA", "away_team": "CHL", "home_score": 1, "away_score": 1, "home_elo": 1935, "away_elo": 1740, "is_host_home": 1, "is_host_away": 0},
    {"year": 2014, "stage": "knockout", "home_team": "COL", "away_team": "URU", "home_score": 2, "away_score": 0, "home_elo": 1820, "away_elo": 1750, "is_host_home": 0, "is_host_away": 0},
    {"year": 2014, "stage": "knockout", "home_team": "NED", "away_team": "MEX", "home_score": 2, "away_score": 1, "home_elo": 1840, "away_elo": 1715, "is_host_home": 0, "is_host_away": 0},
    {"year": 2014, "stage": "knockout", "home_team": "GER", "away_team": "ALG", "home_score": 2, "away_score": 1, "home_elo": 1910, "away_elo": 1580, "is_host_home": 0, "is_host_away": 0},
    {"year": 2014, "stage": "knockout", "home_team": "BRA", "away_team": "COL", "home_score": 2, "away_score": 1, "home_elo": 1935, "away_elo": 1835, "is_host_home": 1, "is_host_away": 0},
    {"year": 2014, "stage": "knockout", "home_team": "FRA", "away_team": "GER", "home_score": 0, "away_score": 1, "home_elo": 1800, "away_elo": 1915, "is_host_home": 0, "is_host_away": 0},
    {"year": 2014, "stage": "knockout", "home_team": "GER", "away_team": "BRA", "home_score": 7, "away_score": 1, "home_elo": 1920, "away_elo": 1945, "is_host_home": 0, "is_host_away": 1},
    {"year": 2014, "stage": "knockout", "home_team": "NED", "away_team": "ARG", "home_score": 0, "away_score": 0, "home_elo": 1860, "away_elo": 1870, "is_host_home": 0, "is_host_away": 0},
    {"year": 2014, "stage": "knockout", "home_team": "GER", "away_team": "ARG", "home_score": 1, "away_score": 0, "home_elo": 1980, "away_elo": 1870, "is_host_home": 0, "is_host_away": 0},

    # 2010 World Cup
    {"year": 2010, "stage": "group", "home_team": "RSA", "away_team": "MEX", "home_score": 1, "away_score": 1, "home_elo": 1530, "away_elo": 1690, "is_host_home": 1, "is_host_away": 0},
    {"year": 2010, "stage": "group", "home_team": "ESP", "away_team": "SUI", "home_score": 0, "away_score": 1, "home_elo": 1960, "away_elo": 1670, "is_host_home": 0, "is_host_away": 0},
    {"year": 2010, "stage": "group", "home_team": "ARG", "away_team": "KOR", "home_score": 4, "away_score": 1, "home_elo": 1870, "away_elo": 1580, "is_host_home": 0, "is_host_away": 0},
    {"year": 2010, "stage": "group", "home_team": "FRA", "away_team": "RSA", "home_score": 1, "away_score": 2, "home_elo": 1720, "away_elo": 1520, "is_host_home": 0, "is_host_away": 1},
    {"year": 2010, "stage": "group", "home_team": "POR", "away_team": "PRK", "home_score": 7, "away_score": 0, "home_elo": 1820, "away_elo": 1400, "is_host_home": 0, "is_host_away": 0},
    {"year": 2010, "stage": "knockout", "home_team": "URU", "away_team": "KOR", "home_score": 2, "away_score": 1, "home_elo": 1720, "away_elo": 1600, "is_host_home": 0, "is_host_away": 0},
    {"year": 2010, "stage": "knockout", "home_team": "GER", "away_team": "ENG", "home_score": 4, "away_score": 1, "home_elo": 1850, "away_elo": 1780, "is_host_home": 0, "is_host_away": 0},
    {"year": 2010, "stage": "knockout", "home_team": "ARG", "away_team": "MEX", "home_score": 3, "away_score": 1, "home_elo": 1900, "away_elo": 1710, "is_host_home": 0, "is_host_away": 0},
    {"year": 2010, "stage": "knockout", "home_team": "URU", "away_team": "GHA", "home_score": 1, "away_score": 1, "home_elo": 1740, "away_elo": 1580, "is_host_home": 0, "is_host_away": 0},
    {"year": 2010, "stage": "knockout", "home_team": "ARG", "away_team": "GER", "home_score": 0, "away_score": 4, "home_elo": 1910, "away_elo": 1870, "is_host_home": 0, "is_host_away": 0},
    {"year": 2010, "stage": "knockout", "home_team": "GER", "away_team": "ESP", "home_score": 0, "away_score": 1, "home_elo": 1920, "away_elo": 1950, "is_host_home": 0, "is_host_away": 0},
    {"year": 2010, "stage": "knockout", "home_team": "NED", "away_team": "ESP", "home_score": 0, "away_score": 1, "home_elo": 1890, "away_elo": 1960, "is_host_home": 0, "is_host_away": 0},

    # 2006 World Cup
    {"year": 2006, "stage": "group", "home_team": "GER", "away_team": "CRC", "home_score": 4, "away_score": 2, "home_elo": 1820, "away_elo": 1540, "is_host_home": 1, "is_host_away": 0},
    {"year": 2006, "stage": "group", "home_team": "ARG", "away_team": "CIV", "home_score": 2, "away_score": 1, "home_elo": 1870, "away_elo": 1590, "is_host_home": 0, "is_host_away": 0},
    {"year": 2006, "stage": "group", "home_team": "ITA", "away_team": "GHA", "home_score": 2, "away_score": 0, "home_elo": 1850, "away_elo": 1520, "is_host_home": 0, "is_host_away": 0},
    {"year": 2006, "stage": "group", "home_team": "BRA", "away_team": "CRO", "home_score": 1, "away_score": 0, "home_elo": 1970, "away_elo": 1700, "is_host_home": 0, "is_host_away": 0},
    {"year": 2006, "stage": "group", "home_team": "FRA", "away_team": "KOR", "home_score": 1, "away_score": 1, "home_elo": 1860, "away_elo": 1560, "is_host_home": 0, "is_host_away": 0},
    {"year": 2006, "stage": "knockout", "home_team": "GER", "away_team": "SWE", "home_score": 2, "away_score": 0, "home_elo": 1845, "away_elo": 1720, "is_host_home": 1, "is_host_away": 0},
    {"year": 2006, "stage": "knockout", "home_team": "ARG", "away_team": "MEX", "home_score": 2, "away_score": 1, "home_elo": 1890, "away_elo": 1715, "is_host_home": 0, "is_host_away": 0},
    {"year": 2006, "stage": "knockout", "home_team": "GER", "away_team": "ARG", "home_score": 1, "away_score": 1, "home_elo": 1860, "away_elo": 1905, "is_host_home": 1, "is_host_away": 0},
    {"year": 2006, "stage": "knockout", "home_team": "ITA", "away_team": "GER", "home_score": 2, "away_score": 0, "home_elo": 1890, "away_elo": 1870, "is_host_home": 0, "is_host_away": 1},
    {"year": 2006, "stage": "knockout", "home_team": "ITA", "away_team": "FRA", "home_score": 1, "away_score": 1, "home_elo": 1930, "away_elo": 1890, "is_host_home": 0, "is_host_away": 0},

    # 2002 World Cup
    {"year": 2002, "stage": "group", "home_team": "FRA", "away_team": "SEN", "home_score": 0, "away_score": 1, "home_elo": 1960, "away_elo": 1500, "is_host_home": 0, "is_host_away": 0},
    {"year": 2002, "stage": "group", "home_team": "KOR", "away_team": "POL", "home_score": 2, "away_score": 0, "home_elo": 1510, "away_elo": 1640, "is_host_home": 1, "is_host_away": 0},
    {"year": 2002, "stage": "group", "home_team": "JPN", "away_team": "BEL", "home_score": 2, "away_score": 2, "home_elo": 1565, "away_elo": 1675, "is_host_home": 1, "is_host_away": 0},
    {"year": 2002, "stage": "group", "home_team": "USA", "away_team": "POR", "home_score": 3, "away_score": 2, "home_elo": 1590, "away_elo": 1820, "is_host_home": 0, "is_host_away": 0},
    {"year": 2002, "stage": "group", "home_team": "GER", "away_team": "KSA", "home_score": 8, "away_score": 0, "home_elo": 1780, "away_elo": 1420, "is_host_home": 0, "is_host_away": 0},
    {"year": 2002, "stage": "knockout", "home_team": "GER", "away_team": "PAR", "home_score": 1, "away_score": 0, "home_elo": 1800, "away_elo": 1670, "is_host_home": 0, "is_host_away": 0},
    {"year": 2002, "stage": "knockout", "home_team": "ESP", "away_team": "IRL", "home_score": 1, "away_score": 1, "home_elo": 1850, "away_elo": 1690, "is_host_home": 0, "is_host_away": 0},
    {"year": 2002, "stage": "knockout", "home_team": "KOR", "away_team": "ITA", "home_score": 2, "away_score": 1, "home_elo": 1580, "away_elo": 1845, "is_host_home": 1, "is_host_away": 0},
    {"year": 2002, "stage": "knockout", "home_team": "GER", "away_team": "USA", "home_score": 1, "away_score": 0, "home_elo": 1810, "away_elo": 1660, "is_host_home": 0, "is_host_away": 0},
    {"year": 2002, "stage": "knockout", "home_team": "KOR", "away_team": "ESP", "home_score": 0, "away_score": 0, "home_elo": 1630, "away_elo": 1860, "is_host_home": 1, "is_host_away": 0},
    {"year": 2002, "stage": "knockout", "home_team": "GER", "away_team": "KOR", "home_score": 1, "away_score": 0, "home_elo": 1825, "away_elo": 1650, "is_host_home": 0, "is_host_away": 1},
    {"year": 2002, "stage": "knockout", "home_team": "BRA", "away_team": "GER", "home_score": 2, "away_score": 0, "home_elo": 1950, "away_elo": 1840, "is_host_home": 0, "is_host_away": 0}
]

# Write to CSV
output_dir = Path(__file__).parent.parent / "data" / "historical"
output_dir.mkdir(parents=True, exist_ok=True)
output_file = output_dir / "past_world_cups.csv"

fieldnames = ["year", "stage", "home_team", "away_team", "home_score", "away_score", "home_elo", "away_elo", "is_host_home", "is_host_away"]
with open(output_file, "w", newline="", encoding="utf-8") as f:
    writer = csv.DictWriter(f, fieldnames=fieldnames)
    writer.writeheader()
    for match in matches:
        writer.writerow(match)

print(f"Generated {len(matches)} historical matches in {output_file}")
