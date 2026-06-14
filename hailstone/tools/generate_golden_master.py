#!/usr/bin/env python3
import os
import sys

def get_metrics(n):
    # Compute max_value and steps using H (standard Collatz)
    val = n
    max_val = n
    steps = 0
    while val != 1:
        if val % 2 == 0:
            val = val // 2
        else:
            val = 3 * val + 1
            if val > max_val:
                max_val = val
        steps += 1
    
    # Compute stopping time (sigma) using T
    # T(x) = (3x + 1) // 2 if x is odd else x // 2
    # sigma is the least k > 0 such that T^k(n) < n
    sigma = 0
    if n >= 3:
        val_t = n
        while True:
            if val_t % 2 == 0:
                val_t = val_t // 2
            else:
                val_t = (3 * val_t + 1) // 2
            sigma += 1
            if val_t < n:
                break
    else:
        sigma = 1 if n == 2 else 0
        
    return max_val, steps, sigma

# Candidates from the paper (including potential OCR typos to verify)
max_value_candidates = [
    3, 7, 15, 27, 255, 447, 639, 703, 1819, 4255, 4591, 9663, 20895, 26623, 31911,
    60975, 77671, 113383, 138367, 159487, 270271, 665215, 704511, 1042431, 1212415,
    1441407, 1875711, 1988859, 2643183, 2684647, 3041127, 3873535, 4637979,
    5656191, 6416623, 6631675, 19638399, 38595583, 80049391, 120080895, 210964383,
    319804831, 1410123943, 8528817511, 12327829503, 23035537407, 45871962271,
    51739336447, 59152641055, 59436135663, 70141259775, 77566362559, 110243094271,
    204430613247, 231913730799, 272025660543, 446559217279, 567839862631,
    871673828443, 2674309547647, 3716509988199, 9016346070511
]

steps_candidates = [
    # Table 7
    2, 6, 18, 54, 31466382, 127456254, 537099606, 1341234558, 9780657630, 63389366646,
    404970804222, 7487118137598,
    # Table 8
    3, 6, 7, 9, 18, 25, 27, 54, 73, 97, 129, 171, 231, 313, 327, 649, 703, 871,
    1161, 2223, 2463, 2919, 3711, 6171, 10971, 13255, 17647, 23529, 26623, 34239,
    35655, 52527, 77031,
    # Table 9
    106239, 142587, 156159, 216367, 230631, 410011, 511935, 626331, 837799, 1117065,
    1501353, 1723519, 2298025, 3064033, 3542887, 3732423, 5649499, 6649279, 8400511,
    11200681, 14934241, 15733191, 31466382, 36791535, 63728127, 127456254, 169941673,
    226588897, 268549803, 537099606, 670617279, 1341234558, 1412987847, 1674652263,
    2610744987, 4578853915, 4890328815,
    # Table 10 (including potential OCR typos and corrections)
    9780657630, 12212032815, 12235060455, 13371194527, 17828259369,
    31664683323, 31694683323,  # one is typo, one is correct
    63389366646, 75128138247, 133561134663, 158294678119, 166763117679,
    202485402111, 404970804222, 426635908975, 568847878633,
    674120078379, 674190078379,  # typo and correct
    881715740415, 989345275647, 1122382791663, 1444338092271,
    1899148164679, 1899148184679,  # typo and correct
    2081751768559, 2775669024745, 3700892032993, 3743559068799, 7487118137598,
    7887663552367, 10516884736489, 14022512981085, 14022512981985, 19536224150271, 26262557464201,
    27667550250351, 38903934249727, 48575069253735, 51173735510107
]

sigma_candidates = [
    2, 3, 7, 27, 703, 10087, 35655, 270271, 362343, 381727, 626331, 1027431, 1126015,
    8088063, 13421671, 20638335, 26716671, 56924955, 63728127, 217740015, 1200991791,
    1827397567, 2788008987, 12235060455, 898696369947, 2081751768559
]

def main():
    # Remove duplicates and sort candidates
    mv_cand = sorted(list(set(max_value_candidates)))
    st_cand = sorted(list(set(steps_candidates)))
    sg_cand = sorted(list(set(sigma_candidates)))

    # Compute actual peaks
    print("Computing Max Value Peaks...")
    max_value_peaks = []
    current_max_val = 0
    # Since search starts at 3 in checkpoint files, we only keep candidates >= 3
    for n in mv_cand:
        if n < 3:
            continue
        max_val, _, _ = get_metrics(n)
        if max_val > current_max_val:
            max_value_peaks.append((n, max_val))
            current_max_val = max_val

    print("Computing Steps Peaks...")
    steps_peaks = []
    current_max_steps = 0
    for n in st_cand:
        if n < 3:
            continue
        _, steps, _ = get_metrics(n)
        if steps > current_max_steps:
            steps_peaks.append((n, steps))
            current_max_steps = steps

    print("Computing Sigma Peaks...")
    sigma_peaks = []
    current_max_sigma = 0
    for n in sg_cand:
        if n < 3:
            continue
        _, _, sigma = get_metrics(n)
        if sigma > current_max_sigma:
            sigma_peaks.append((n, sigma))
            current_max_sigma = sigma

    # Summaries
    last_num = 56000000000000  # 56 trillion search limit from paper
    global_max_val = max_value_peaks[-1][1]
    global_max_steps = steps_peaks[-1][1]
    global_max_sigma = sigma_peaks[-1][1]

    # Write output to golden_master.chk
    hailstone_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out_path = os.path.join(hailstone_dir, "golden_master.chk")

    with open(out_path, "w") as f:
        f.write(f"last_num: {last_num}\n")
        f.write(f"max_value: {global_max_val}\n")
        f.write(f"max_steps: {global_max_steps}\n")
        f.write(f"max_sigma: {global_max_sigma}\n\n")

        f.write("max_value_peaks:\n")
        for n, val in max_value_peaks:
            f.write(f"{n} {val}\n")
        f.write("\n")

        f.write("steps_peaks:\n")
        for n, steps in steps_peaks:
            f.write(f"{n} {steps}\n")
        f.write("\n")

        f.write("sigma_peaks:\n")
        for n, sigma in sigma_peaks:
            f.write(f"{n} {sigma}\n")
        f.write("\n")

    print(f"Generated golden master file at: {out_path}")
    print(f"Number of max_value peaks: {len(max_value_peaks)}")
    print(f"Number of steps peaks: {len(steps_peaks)}")
    print(f"Number of sigma peaks: {len(sigma_peaks)}")

if __name__ == "__main__":
    main()
