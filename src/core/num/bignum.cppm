export module rstd.core:num.bignum;
import :array;
import rstd.basic;

namespace rstd::num::bignum
{

template<usize Capacity>
class FixedBig {
    usize                size_ = 0;
    array<u32, Capacity> base_ {};

    [[noreturn]]
    static void capacity_error() {
        __builtin_trap();
    }

public:
    static auto from_small(u32 value) noexcept -> FixedBig {
        FixedBig result;
        result.base_[0] = value;
        result.size_    = 1;
        return result;
    }

    static auto from_u64(u64 value) noexcept -> FixedBig {
        FixedBig result;
        while (value != 0) {
            result.base_[result.size_++] = static_cast<u32>(value);
            value >>= 32;
        }
        return result;
    }

    auto compare(FixedBig const& other) const noexcept -> i8 {
        const usize size = size_ > other.size_ ? size_ : other.size_;
        for (usize i = size; i != 0; --i) {
            if (base_[i - 1] < other.base_[i - 1]) return -1;
            if (base_[i - 1] > other.base_[i - 1]) return 1;
        }
        return 0;
    }

    auto is_zero() const noexcept -> bool {
        for (usize i = 0; i < size_; ++i) {
            if (base_[i] != 0) return false;
        }
        return true;
    }

    auto bit_length() const noexcept -> usize {
        for (usize i = size_; i != 0; --i) {
            if (base_[i - 1] != 0) {
                return (i - 1) * 32 + 32 - static_cast<usize>(__builtin_clz(base_[i - 1]));
            }
        }
        return 0;
    }

    void add(FixedBig const& other) {
        usize size  = size_ > other.size_ ? size_ : other.size_;
        u64   carry = 0;
        for (usize i = 0; i < size; ++i) {
            const u64 value = static_cast<u64>(base_[i]) + other.base_[i] + carry;
            base_[i]        = static_cast<u32>(value);
            carry           = value >> 32;
        }
        if (carry != 0) {
            if (size == Capacity) capacity_error();
            base_[size++] = 1;
        }
        size_ = size;
    }

    void add_small(u32 other) {
        usize index = 0;
        u64   carry = other;
        while (carry != 0) {
            if (index == Capacity) capacity_error();
            const u64 value = static_cast<u64>(base_[index]) + carry;
            base_[index]    = static_cast<u32>(value);
            carry           = value >> 32;
            ++index;
        }
        if (size_ < index) size_ = index;
    }

    void sub(FixedBig const& other) {
        const usize size   = size_ > other.size_ ? size_ : other.size_;
        u64         borrow = 0;
        for (usize i = 0; i < size; ++i) {
            const u64 lhs = base_[i];
            const u64 rhs = static_cast<u64>(other.base_[i]) + borrow;
            base_[i]      = static_cast<u32>(lhs - rhs);
            borrow        = lhs < rhs;
        }
        if (borrow != 0) capacity_error();
        size_ = size;
    }

    void mul_small(u32 other) {
        usize size  = size_;
        u64   carry = 0;
        for (usize i = 0; i < size; ++i) {
            const u64 value = static_cast<u64>(base_[i]) * other + carry;
            base_[i]        = static_cast<u32>(value);
            carry           = value >> 32;
        }
        if (carry != 0) {
            if (size == Capacity) capacity_error();
            base_[size++] = static_cast<u32>(carry);
        }
        size_ = size;
    }

    void mul_pow2(usize bits) {
        const usize digits    = bits / 32;
        const usize shift     = bits % 32;
        const usize old_size  = size_;
        usize       next_size = old_size + digits;
        if (next_size > Capacity) capacity_error();

        for (usize i = old_size; i != 0; --i) base_[i - 1 + digits] = base_[i - 1];
        for (usize i = 0; i < digits; ++i) base_[i] = 0;

        if (shift != 0 && next_size != 0) {
            const u32 overflow = base_[next_size - 1] >> (32 - shift);
            if (overflow != 0) {
                if (next_size == Capacity) capacity_error();
                base_[next_size++] = overflow;
            }
            for (usize i = next_size - (overflow != 0); i > digits + 1; --i) {
                const usize index = i - 1;
                base_[index]      = (base_[index] << shift) | (base_[index - 1] >> (32 - shift));
            }
            base_[digits] <<= shift;
        }
        size_ = next_size;
    }

    void mul_pow5(usize exponent) {
        while (exponent >= 13) {
            mul_small(1'220'703'125);
            exponent -= 13;
        }
        u32 rest = 1;
        while (exponent-- != 0) rest *= 5;
        mul_small(rest);
    }

    void mul_digits(const u32* other, usize other_len) {
        array<u32, Capacity> result {};
        usize                result_size = 0;
        for (usize i = 0; i < size_; ++i) {
            if (base_[i] == 0) continue;
            u64 carry = 0;
            for (usize j = 0; j < other_len; ++j) {
                if (i + j >= Capacity) capacity_error();
                const u64 value = static_cast<u64>(base_[i]) * other[j] + result[i + j] + carry;
                result[i + j]   = static_cast<u32>(value);
                carry           = value >> 32;
            }
            usize size = other_len;
            if (carry != 0) {
                if (i + size >= Capacity) capacity_error();
                result[i + size++] = static_cast<u32>(carry);
            }
            if (result_size < i + size) result_size = i + size;
        }
        base_ = result;
        size_ = result_size;
    }

    auto div_rem_small(u32 other) -> u32 {
        u64 remainder = 0;
        for (usize i = size_; i != 0; --i) {
            const u64 value = (remainder << 32) | base_[i - 1];
            base_[i - 1]    = static_cast<u32>(value / other);
            remainder       = value % other;
        }
        return static_cast<u32>(remainder);
    }
};

} // namespace rstd::num::bignum
