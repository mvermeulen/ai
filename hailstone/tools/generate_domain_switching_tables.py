import sys

def main():
    max_val = (1 << 128) - 1
    
    print("// Precomputed powers of 3 lookup table (up to 3^40)")
    print("const uint128 lut3[] = {")
    for alpha in range(41):
        pow3 = 3**alpha
        low = pow3 & 0xFFFFFFFFFFFFFFFF
        high = (pow3 >> 64) & 0xFFFFFFFFFFFFFFFF
        print(f"    uint128(0x{low:016x}ULL, 0x{high:016x}ULL), // 3^{alpha}")
    print("};")
    print()
    
    print("// Maximum safe k = (2^128 - 1) / 3^alpha to avoid overflow")
    print("const uint128 max_safe_k[] = {")
    for alpha in range(41):
        pow3 = 3**alpha
        safe_k = max_val // pow3
        low = safe_k & 0xFFFFFFFFFFFFFFFF
        high = (safe_k >> 64) & 0xFFFFFFFFFFFFFFFF
        print(f"    uint128(0x{low:016x}ULL, 0x{high:016x}ULL), // for alpha={alpha}")
    print("};")

if __name__ == "__main__":
    main()
