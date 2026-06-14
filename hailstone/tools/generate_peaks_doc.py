#!/usr/bin/env python3
import os
import sys

def format_number(val_str):
    try:
        val = int(val_str)
        if val > 10**6:
            # Format with commas and also add scientific notation for very large numbers
            formatted = f"{val:,}"
            if val > 10**12:
                formatted += f" (≈ {val:.3e})"
            return formatted
        return f"{val:,}"
    except ValueError:
        return val_str

def get_hailstone_path(start):
    x = start
    peak_val = start
    steps = 0
    t_steps = 0
    has_stopping = False
    stopping_time_val = 0
    
    while x > 1:
        if x % 2 == 1:
            next_x = 3 * x + 1
            steps += 1
            if next_x > peak_val:
                peak_val = next_x
            x = next_x // 2
            steps += 1
            t_steps += 1
            if not has_stopping and x < start:
                stopping_time_val = x
                has_stopping = True
        else:
            x = x // 2
            steps += 1
            t_steps += 1
            if not has_stopping and x < start:
                stopping_time_val = x
                has_stopping = True
                
    path = []
    x = start
    peak_marked = False
    stopping_marked = False
    
    if x == peak_val:
        path.append('^')
        peak_marked = True
        
    while x > 2:
        if x % 2 == 1:
            temp = 3 * x + 1
            if temp == peak_val:
                path.append('*^')
                peak_marked = True
                x = temp
            else:
                x = temp // 2
                path.append('*')
                if has_stopping and not stopping_marked and x == stopping_time_val:
                    path.append('|')
                    stopping_marked = True
                if not peak_marked and x == peak_val:
                    path.append('^')
                    peak_marked = True
        else:
            x = x // 2
            path.append('/')
            if has_stopping and not stopping_marked and x == stopping_time_val:
                path.append('|')
                stopping_marked = True
            if not peak_marked and x == peak_val:
                path.append('^')
                peak_marked = True
    return "".join(path)

def generate_markdown(chk_path, md_path):
    if not os.path.exists(chk_path):
        print(f"Error: Checkpoint file {chk_path} not found.")
        sys.exit(1)

    last_num = "0"
    max_val = "0"
    max_steps = "0"
    max_sigma = "0"

    max_value_peaks = []
    steps_peaks = []
    sigma_peaks = []

    current_section = None

    with open(chk_path, "r") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue

            if line.startswith("last_num:"):
                last_num = line.split(":", 1)[1].strip()
            elif line.startswith("max_value:"):
                max_val = line.split(":", 1)[1].strip()
            elif line.startswith("max_steps:"):
                max_steps = line.split(":", 1)[1].strip()
            elif line.startswith("max_sigma:"):
                max_sigma = line.split(":", 1)[1].strip()
            elif line == "max_value_peaks:":
                current_section = "max_value"
            elif line == "steps_peaks:":
                current_section = "steps"
            elif line == "sigma_peaks:":
                current_section = "sigma"
            else:
                parts = line.split()
                if len(parts) == 2:
                    start_val, metric_val = parts
                    record = (start_val, metric_val)
                    if current_section == "max_value":
                        max_value_peaks.append(record)
                    elif current_section == "steps":
                        steps_peaks.append(record)
                    elif current_section == "sigma":
                        sigma_peaks.append(record)

    # Convert last_num to a readable range limit
    try:
        last_val = int(last_num)
        blocks = last_val / (2**32)
        range_desc = f"`[3, {last_val:,}]` (up to block {blocks:.2f})"
    except Exception:
        range_desc = f"`[3, {last_num}]`"

    md_content = []
    md_content.append("# Hailstone Search Peak Records")
    md_content.append("")
    md_content.append("This document tracks the record-breaking trajectory peaks discovered during the Hailstone search.")
    md_content.append("As the search proceeds, this document is updated to record the historical peaks for each of the three tracked metrics.")
    md_content.append("")
    md_content.append("## Search Status")
    md_content.append("")
    md_content.append(f"- **Current Range Searched**: {range_desc}")
    md_content.append(f"- **Global Max Value**: {format_number(max_val)}")
    md_content.append(f"- **Global Max Steps**: {format_number(max_steps)}")
    md_content.append(f"- **Global Max Stopping Time ($\\sigma$)**: {format_number(max_sigma)}")
    md_content.append("")
    md_content.append("---")
    md_content.append("")
    md_content.append("## 1. Max Value Peaks")
    md_content.append("Tracked when a starting value $n$ generates a trajectory whose maximum intermediate value exceeds any maximum value seen in previous trajectories.")
    md_content.append("")
    md_content.append("| Starting Number ($n$) | Maximum Intermediate Value |")
    md_content.append("| :--- | :--- |")
    for start, metric in max_value_peaks:
        md_content.append(f"| {format_number(start)} | {format_number(metric)} |")
    md_content.append("")
    md_content.append("---")
    md_content.append("")
    md_content.append("## 2. Steps Peaks")
    md_content.append("Tracked when a starting value $n$ takes more total steps to reach $1$ than any previous starting value.")
    md_content.append("")
    md_content.append("| Starting Number ($n$) | Total Steps | First 8 Path Steps | Relationship / Flags |")
    md_content.append("| :--- | :---: | :--- | :--- |")
    
    # Pre-compute paths and relationships
    peak_paths = {}
    pure_paths = {}
    for start, metric in steps_peaks:
        n = int(start)
        p = get_hailstone_path(n)
        peak_paths[n] = p
        pure_paths[n] = p.replace('|', '').replace('^', '')

    relationships = {}
    for start, metric in steps_peaks:
        n = int(start)
        pure_p = pure_paths[n]
        rel_str = ""
        
        if pure_p.startswith('/'):
            suffix = pure_p[1:]
            matches = [q for q in pure_paths if pure_paths[q] == suffix]
            if matches:
                rel_str = f"Even peak (prepend `/` to {matches[0]:,})"
            else:
                rel_str = "Even peak"
        elif pure_p.startswith('*/'):
            suffix = pure_p[2:]
            matches = [q for q in pure_paths if pure_paths[q] == suffix]
            if matches:
                rel_str = f"$(3x+1)/4$ peak (prepend `*/` to {matches[0]:,})"
            else:
                q_val = (3 * n + 1) // 4
                rel_str = f"$(3x+1)/4$ peak (prepend `*/` to {q_val:,}*)"
                
        relationships[n] = rel_str

    for start, metric in steps_peaks:
        n = int(start)
        path_str = peak_paths[n]
        prefix = path_str[:8]
        rel = relationships[n]
        md_content.append(f"| {format_number(start)} | {format_number(metric)} | `{prefix}` | {rel} |")
    md_content.append("")
    md_content.append("\\* Note: The successor value $Q$ is not itself a historical steps peak (a counter-example to direct peak-to-peak prediction). See [3x_plus_1_over_4_path_investigation.md](file:///home/mev/source/ai/hailstone/doc/3x_plus_1_over_4_path_investigation.md) for the mathematical analysis.")
    md_content.append("")
    md_content.append("---")
    md_content.append("")
    md_content.append("## 3. Stopping Time ($\\sigma$) Peaks")
    md_content.append("Tracked when a starting value $n$ takes more steps to drop below its starting value (stopping time $\\sigma$) than any previous starting value.")
    md_content.append("")
    md_content.append("| Starting Number ($n$) | Stopping Time ($\\sigma$) |")
    md_content.append("| :--- | :--- |")
    for start, metric in sigma_peaks:
        md_content.append(f"| {format_number(start)} | {format_number(metric)} |")
    md_content.append("")

    with open(md_path, "w") as f:
        f.write("\n".join(md_content))

    print(f"Successfully generated peaks document at {md_path}")

if __name__ == "__main__":
    hailstone_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    chk = os.path.join(hailstone_dir, "hailstone.chk")
    md = os.path.join(hailstone_dir, "doc", "hailstone_peaks.md")
    
    if len(sys.argv) > 1:
        chk = sys.argv[1]
    if len(sys.argv) > 2:
        md = sys.argv[2]
        
    generate_markdown(chk, md)
