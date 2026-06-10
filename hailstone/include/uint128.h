#ifndef HAILSTONE_UINT128_H
#define HAILSTONE_UINT128_H

#include <cstdint>

#if defined(__HIPCC__) || defined(__CUDACC__)
#define HD_ATTR __host__ __device__ inline
#else
#define HD_ATTR inline
#endif

struct uint128 {
    uint64_t low;
    uint64_t high;

    HD_ATTR uint128() : low(0), high(0) {}
    HD_ATTR uint128(uint64_t l) : low(l), high(0) {}
    HD_ATTR uint128(uint64_t l, uint64_t h) : low(l), high(h) {}
};

HD_ATTR bool operator==(const uint128& a, const uint128& b) {
    return a.low == b.low && a.high == b.high;
}

HD_ATTR bool operator!=(const uint128& a, const uint128& b) {
    return !(a == b);
}

HD_ATTR bool operator<(const uint128& a, const uint128& b) {
    if (a.high != b.high) return a.high < b.high;
    return a.low < b.low;
}

HD_ATTR bool operator<=(const uint128& a, const uint128& b) {
    return !(b < a);
}

HD_ATTR bool operator>(const uint128& a, const uint128& b) {
    return b < a;
}

HD_ATTR bool operator>=(const uint128& a, const uint128& b) {
    return !(a < b);
}

HD_ATTR uint128 operator+(const uint128& a, const uint128& b) {
    uint128 res;
    res.low = a.low + b.low;
    res.high = a.high + b.high + (res.low < a.low ? 1 : 0);
    return res;
}

HD_ATTR uint128 operator-(const uint128& a, const uint128& b) {
    uint128 res;
    res.low = a.low - b.low;
    uint64_t borrow = (a.low < b.low) ? 1 : 0;
    res.high = a.high - b.high - borrow;
    return res;
}

HD_ATTR uint128 add_check_overflow(uint128 a, uint128 b, bool& overflow) {
    uint128 res;
    res.low = a.low + b.low;
    uint64_t carry = (res.low < a.low) ? 1 : 0;
    res.high = a.high + b.high + carry;
    bool carry_high = (res.high < a.high) || (carry && res.high == a.high);
    overflow = carry_high;
    return res;
}

HD_ATTR uint128 shift_left_1(uint128 a, bool& overflow) {
    uint128 res;
    overflow = (a.high & 0x8000000000000000ULL) != 0;
    res.high = (a.high << 1) | (a.low >> 63);
    res.low = a.low << 1;
    return res;
}

HD_ATTR uint128 shift_right(uint128 a, int p) {
    uint128 res = {0, 0};
    if (p <= 0) {
        return a;
    } else if (p < 64) {
        res.low = (a.low >> p) | (a.high << (64 - p));
        res.high = a.high >> p;
    } else if (p < 128) {
        res.low = a.high >> (p - 64);
        res.high = 0;
    }
    return res;
}

HD_ATTR int ctz64(uint64_t val) {
    return __builtin_ctzll(val);
}

HD_ATTR int count_trailing_zeros(uint128 a) {
    if (a.low != 0) {
        return ctz64(a.low);
    } else if (a.high != 0) {
        return 64 + ctz64(a.high);
    } else {
        return 128; // zero value has 128 trailing zeros
    }
}

HD_ATTR bool check_mul3_add1_overflow(uint128 n) {
    // Max value where 3n + 1 does not overflow 128 bits is floor((2^128 - 2)/3)
    // which in hex is exactly: 0x5555555555555555_5555555555555554
    if (n.high > 0x5555555555555555ULL) return true;
    if (n.high == 0x5555555555555555ULL && n.low > 0x5555555555555554ULL) return true;
    return false;
}

HD_ATTR uint128 mul3_add1(uint128 n, bool& overflow) {
    if (check_mul3_add1_overflow(n)) {
        overflow = true;
        return uint128(0, 0);
    }
    overflow = false;
    // 3n + 1 = (n << 1) + n + 1
    bool temp_of = false;
    uint128 n2 = shift_left_1(n, temp_of);
    uint128 n3 = n2 + n;
    return n3 + uint128(1);
}

#endif // HAILSTONE_UINT128_H
