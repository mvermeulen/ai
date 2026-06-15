#include "uint128.h"
#include "peak_predictor.h"
#include <iostream>
#include <random>
#include <cassert>

// Convert unsigned __int128 to custom uint128
uint128 to_uint128(unsigned __int128 val) {
    uint128 res;
    res.low = static_cast<uint64_t>(val);
    res.high = static_cast<uint64_t>(val >> 64);
    return res;
}

// Convert custom uint128 to unsigned __int128
unsigned __int128 to_int128(uint128 val) {
    unsigned __int128 res = val.high;
    res = (res << 64) | val.low;
    return res;
}

void test_constructors() {
    uint128 a;
    assert(a.low == 0 && a.high == 0);

    uint128 b(42);
    assert(b.low == 42 && b.high == 0);

    uint128 c(100, 200);
    assert(c.low == 100 && c.high == 200);
}

void test_comparisons() {
    uint128 a(10, 5);
    uint128 b(10, 5);
    uint128 c(11, 5);
    uint128 d(10, 6);

    assert(a == b);
    assert(a != c);
    assert(a < c);
    assert(a <= c);
    assert(c > a);
    assert(c >= a);

    assert(a < d);
    assert(d > a);
    assert(d >= a);
    assert(d != a);
}

void test_addition() {
    uint128 a(0xFFFFFFFFFFFFFFFFULL, 0);
    uint128 b(1, 0);
    uint128 c = a + b;
    assert(c.low == 0);
    assert(c.high == 1);

    bool overflow = false;
    uint128 d = add_check_overflow(uint128(0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL), uint128(1, 0), overflow);
    assert(overflow == true);
    assert(d.low == 0 && d.high == 0);

    overflow = true;
    uint128 e = add_check_overflow(uint128(0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFEULL), uint128(1, 0), overflow);
    assert(overflow == false);
    assert(e.low == 0 && e.high == 0xFFFFFFFFFFFFFFFFULL);
}

void test_subtraction() {
    uint128 a(0, 1);
    uint128 b(1, 0);
    uint128 c = a - b;
    assert(c.low == 0xFFFFFFFFFFFFFFFFULL);
    assert(c.high == 0);

    uint128 d(10, 5);
    uint128 e(10, 5);
    uint128 f = d - e;
    assert(f.low == 0 && f.high == 0);
}


void test_shifts() {
    uint128 a(0x0000000000000001ULL, 0x8000000000000000ULL);
    bool overflow = false;
    uint128 b = shift_left_1(a, overflow);
    assert(overflow == true);
    assert(b.low == 2);
    assert(b.high == 0); // High bit lost on overflow

    uint128 c(0x8000000000000000ULL, 0x0000000000000001ULL);
    overflow = false;
    uint128 d = shift_left_1(c, overflow);
    assert(overflow == false);
    assert(d.low == 0);
    assert(d.high == 3);

    // Right shifts
    uint128 x(0x0F0F0F0F0F0F0F0FULL, 0xF0F0F0F0F0F0F0F0ULL);
    assert(shift_right(x, 0) == x);
    assert(shift_right(x, 4) == uint128(0x00F0F0F0F0F0F0F0ULL, 0x0F0F0F0F0F0F0F0FULL));
    assert(shift_right(x, 64) == uint128(0xF0F0F0F0F0F0F0F0ULL, 0));
    assert(shift_right(x, 68) == uint128(0x0F0F0F0F0F0F0F0FULL, 0));
    assert(shift_right(x, 128) == uint128(0, 0));
}

void test_ctz() {
    assert(count_trailing_zeros(uint128(0, 0)) == 128);
    assert(count_trailing_zeros(uint128(1, 0)) == 0);
    assert(count_trailing_zeros(uint128(2, 0)) == 1);
    assert(count_trailing_zeros(uint128(0x8000000000000000ULL, 0)) == 63);
    assert(count_trailing_zeros(uint128(0, 1)) == 64);
    assert(count_trailing_zeros(uint128(0, 2)) == 65);
    assert(count_trailing_zeros(uint128(0, 0x8000000000000000ULL)) == 127);
}

void test_mul3_add1() {
    uint128 max_safe(0x5555555555555554ULL, 0x5555555555555555ULL);
    assert(check_mul3_add1_overflow(max_safe) == false);

    uint128 min_unsafe(0x5555555555555555ULL, 0x5555555555555555ULL);
    assert(check_mul3_add1_overflow(min_unsafe) == true);

    bool overflow = false;
    uint128 r1 = mul3_add1(uint128(7), overflow);
    assert(overflow == false);
    assert(r1 == uint128(22));

    uint128 r2 = mul3_add1(max_safe, overflow);
    assert(overflow == false);
    // 3 * max_safe + 1 = 3 * ((2^128 - 1)/3 - 1) + 1 = 2^128 - 3
    assert(r2.low == 0xFFFFFFFFFFFFFFFDULL && r2.high == 0xFFFFFFFFFFFFFFFFULL);

    uint128 r3 = mul3_add1(min_unsafe, overflow);
    assert(overflow == true);
}

void test_fuzz() {
    std::mt19937_64 rng(12345);
    for (int i = 0; i < 100000; ++i) {
        unsigned __int128 a_raw = (static_cast<unsigned __int128>(rng()) << 64) | rng();
        unsigned __int128 b_raw = (static_cast<unsigned __int128>(rng()) << 64) | rng();

        uint128 a = to_uint128(a_raw);
        uint128 b = to_uint128(b_raw);

        // Test comparison
        assert((a == b) == (a_raw == b_raw));
        assert((a != b) == (a_raw != b_raw));
        assert((a < b) == (a_raw < b_raw));
        assert((a <= b) == (a_raw <= b_raw));
        assert((a > b) == (a_raw > b_raw));
        assert((a >= b) == (a_raw >= b_raw));

        // Test addition
        unsigned __int128 sum_raw = a_raw + b_raw;
        uint128 sum = a + b;
        assert(to_int128(sum) == sum_raw);

        // Test subtraction
        if (a_raw >= b_raw) {
            unsigned __int128 sub_raw = a_raw - b_raw;
            uint128 sub = a - b;
            assert(to_int128(sub) == sub_raw);
        }

        // Test add with overflow check
        bool overflow = false;
        uint128 sum_check = add_check_overflow(a, b, overflow);
        bool expected_overflow = (a_raw > (~b_raw));
        assert(overflow == expected_overflow);
        if (!overflow) {
            assert(sum_check == sum);
        }

        // Test shift_right
        int shift_amt = rng() % 130;
        unsigned __int128 shr_raw = (shift_amt >= 128) ? 0 : (a_raw >> shift_amt);
        uint128 shr = shift_right(a, shift_amt);
        assert(to_int128(shr) == shr_raw);

        // Test count_trailing_zeros
        int ctz_expected = 128;
        if (a_raw != 0) {
            unsigned __int128 temp = a_raw;
            ctz_expected = 0;
            while ((temp & 1) == 0) {
                ctz_expected++;
                temp >>= 1;
            }
        }
        assert(count_trailing_zeros(a) == ctz_expected);

        // Test mul3_add1
        bool overflow_mul = false;
        uint128 res_mul = mul3_add1(a, overflow_mul);
        unsigned __int128 expected_mul = a_raw * 3 + 1;
        bool expected_overflow_mul = false;
        if (a_raw > (static_cast<unsigned __int128>(-1) - 1) / 3) {
            expected_overflow_mul = true;
        }
        assert(overflow_mul == expected_overflow_mul);
        if (!overflow_mul) {
            assert(to_int128(res_mul) == expected_mul);
        }
    }
}

void test_peak_predictor_even_heuristic() {
    PeakPredictor predictor;
    // Add peak 27 (steps = 111)
    predictor.add_confirmed_peak(uint128(27), 111);

    // Predictor should have generated:
    // From 27 (27 % 3 == 0): P = 54 (steps = 112)
    bool found_54 = false;
    for (const auto& p : predictor.active_predictions) {
        if (p.pred_n == uint128(54)) {
            assert(p.pred_steps == 112);
            found_54 = true;
        }
    }
    assert(found_54);

    // Confirm peak 54. Since 54 is even, it should trigger the even peak + 1 heuristic:
    // - checks if 55 has the same path (which it does, both merge at 94 in 7 steps).
    // - predicts from 55 (55 % 3 == 1): P = (4 * 55 - 1) / 3 = 73 (steps = 112 + 3 = 115).
    predictor.add_confirmed_peak(uint128(54), 112);

    bool found_73 = false;
    for (const auto& p : predictor.active_predictions) {
        if (p.pred_n == uint128(73)) {
            assert(p.pred_steps == 115);
            found_73 = true;
        }
    }
    assert(found_73);
}

int main() {
    std::cout << "Running uint128 unit tests..." << std::endl;
    test_constructors();
    test_comparisons();
    test_addition();
    test_subtraction();
    test_shifts();
    test_ctz();
    test_mul3_add1();
    test_fuzz();
    test_peak_predictor_even_heuristic();
    std::cout << "All uint128 unit tests passed successfully!" << std::endl;
    return 0;
}
