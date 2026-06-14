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

def get_path_states_and_ops(start):
    x = start
    peak_val = start
    has_stopping = False
    stopping_time_val = 0
    
    temp_x = start
    while temp_x > 1:
        if temp_x % 2 == 1:
            next_x = 3 * temp_x + 1
            if next_x > peak_val:
                peak_val = next_x
            temp_x = next_x // 2
            if not has_stopping and temp_x < start:
                stopping_time_val = temp_x
                has_stopping = True
        else:
            temp_x = temp_x // 2
            if not has_stopping and temp_x < start:
                stopping_time_val = temp_x
                has_stopping = True
                
    states = [start]
    pure_ops = []
    x = start
    while x > 2:
        if x % 2 == 1:
            temp = 3 * x + 1
            if temp == peak_val:
                pure_ops.append('*')
                states.append(temp)
                pure_ops.append('/')
                x = temp // 2
                states.append(x)
            else:
                pure_ops.append('*')
                x = temp // 2
                states.append(x)
        else:
            pure_ops.append('/')
            x = x // 2
            states.append(x)
    return states, pure_ops

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
    md_content.append("| Starting Number ($n$) | Total Steps | Steps to Merge | Merge Path Prefix | Relationship / Flags |")
    md_content.append("| :--- | :---: | :---: | :--- | :--- |")
    
    # Pre-compute path states and pure operations
    states_dict = {}
    ops_dict = {}
    pure_paths = {}
    for start, metric in steps_peaks:
        n = int(start)
        states, ops = get_path_states_and_ops(n)
        states_dict[n] = states
        ops_dict[n] = ops
        pure_paths[n] = "".join(ops)

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

    for idx, (start, metric) in enumerate(steps_peaks):
        n = int(start)
        rel = relationships[n]
        
        if idx == 0:
            md_content.append(f"| {format_number(start)} | {format_number(metric)} | - | - | {rel} |")
        else:
            states_p = states_dict[n]
            ops_p = ops_dict[n]
            best_i = None
            best_r = None
            
            for start_r, _ in steps_peaks[:idx]:
                r = int(start_r)
                states_r_set = set(states_dict[r])
                for i, val in enumerate(states_p):
                    if val in states_r_set:
                        if best_i is None or i < best_i:
                            best_i = i
                            best_r = r
                        break
            
            if best_i is not None:
                prefix = "".join(ops_p[:best_i])
                disp_prefix = prefix[:8] + ("..." if len(prefix) > 8 else "")
                md_content.append(f"| {format_number(start)} | {format_number(metric)} | {best_i} | `{disp_prefix}` | {rel} |")
            else:
                md_content.append(f"| {format_number(start)} | {format_number(metric)} | ? | ? | {rel} |")
                
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
