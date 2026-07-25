export module rstd.core:num.bignum;
import :num.types;
import :array;
import :intrinsics;
import rstd.basic;

namespace rstd::num::bignum
{

template<rstd::size_t Capacity>
class FixedBig {
    rstd::size_t   size_ = 0;
    rstd::uint32_t base_[Capacity] {};

    [[noreturn]]
    static void capacity_error() {
        rstd::intrinsics::abort();
    }

public:
    static auto from_small(rstd::uint32_t value) noexcept -> FixedBig {
        FixedBig result;
        result.base_[0] = value;
        result.size_    = rstd::size_t(1);
        return result;
    }

    static auto from_u64(rstd::uint64_t value) noexcept -> FixedBig {
        FixedBig result;
        while (value != rstd::uint64_t(0)) {
            result.base_[result.size_++] = static_cast<rstd::uint32_t>(value);
            value >>= rstd::uint32_t(32);
        }
        return result;
    }

    auto compare(FixedBig const& other) const noexcept -> int {
        auto const size = size_ > other.size_ ? size_ : other.size_;
        for (rstd::size_t i = size; i != rstd::size_t(0); --i) {
            if (base_[i - rstd::size_t(1)] < other.base_[i - rstd::size_t(1)]) return -1;
            if (base_[i - rstd::size_t(1)] > other.base_[i - rstd::size_t(1)]) return 1;
        }
        return 0;
    }

    auto is_zero() const noexcept -> bool {
        for (rstd::size_t i = 0; i < size_; ++i) {
            if (base_[i] != rstd::uint32_t(0)) return false;
        }
        return true;
    }

    auto bit_length() const noexcept -> rstd::size_t {
        for (rstd::size_t i = size_; i != rstd::size_t(0); --i) {
            if (base_[i - rstd::size_t(1)] != rstd::uint32_t(0)) {
                return (i - rstd::size_t(1)) * rstd::size_t(32) + rstd::size_t(32) -
                       static_cast<rstd::size_t>(__builtin_clz(base_[i - rstd::size_t(1)]));
            }
        }
        return rstd::size_t(0);
    }

    void add(FixedBig const& other) {
        rstd::size_t   size  = size_ > other.size_ ? size_ : other.size_;
        rstd::uint64_t carry = 0;
        for (rstd::size_t i = 0; i < size; ++i) {
            auto const value = static_cast<rstd::uint64_t>(base_[i]) + other.base_[i] + carry;
            base_[i]         = static_cast<rstd::uint32_t>(value);
            carry            = value >> rstd::uint32_t(32);
        }
        if (carry != rstd::uint64_t(0)) {
            if (size == Capacity) capacity_error();
            base_[size++] = rstd::uint32_t(1);
        }
        size_ = size;
    }

    void add_small(rstd::uint32_t other) {
        rstd::size_t   index = 0;
        rstd::uint64_t carry = other;
        while (carry != rstd::uint64_t(0)) {
            if (index == Capacity) capacity_error();
            auto const value = static_cast<rstd::uint64_t>(base_[index]) + carry;
            base_[index]     = static_cast<rstd::uint32_t>(value);
            carry            = value >> rstd::uint32_t(32);
            ++index;
        }
        if (size_ < index) size_ = index;
    }

    void sub(FixedBig const& other) {
        auto const     size   = size_ > other.size_ ? size_ : other.size_;
        rstd::uint64_t borrow = 0;
        for (rstd::size_t i = 0; i < size; ++i) {
            auto const lhs = static_cast<rstd::uint64_t>(base_[i]);
            auto const rhs = static_cast<rstd::uint64_t>(other.base_[i]) + borrow;
            base_[i]       = static_cast<rstd::uint32_t>(lhs - rhs);
            borrow         = lhs < rhs ? rstd::uint64_t(1) : rstd::uint64_t(0);
        }
        if (borrow != rstd::uint64_t(0)) capacity_error();
        size_ = size;
    }

    void mul_small(rstd::uint32_t other) {
        rstd::size_t   size  = size_;
        rstd::uint64_t carry = 0;
        for (rstd::size_t i = 0; i < size; ++i) {
            auto const value = static_cast<rstd::uint64_t>(base_[i]) * other + carry;
            base_[i]         = static_cast<rstd::uint32_t>(value);
            carry            = value >> rstd::uint32_t(32);
        }
        if (carry != rstd::uint64_t(0)) {
            if (size == Capacity) capacity_error();
            base_[size++] = static_cast<rstd::uint32_t>(carry);
        }
        size_ = size;
    }

    void mul_pow2(rstd::size_t bits) {
        auto const digits    = bits / rstd::size_t(32);
        auto const shift     = static_cast<rstd::uint32_t>(bits % rstd::size_t(32));
        auto const old_size  = size_;
        auto       next_size = old_size + digits;
        if (next_size > Capacity) capacity_error();

        for (rstd::size_t i = old_size; i != rstd::size_t(0); --i)
            base_[i - rstd::size_t(1) + digits] = base_[i - rstd::size_t(1)];
        for (rstd::size_t i = 0; i < digits; ++i) base_[i] = rstd::uint32_t(0);

        if (shift != rstd::uint32_t(0) && next_size != rstd::size_t(0)) {
            auto const overflow =
                base_[next_size - rstd::size_t(1)] >> (rstd::uint32_t(32) - shift);
            if (overflow != rstd::uint32_t(0)) {
                if (next_size == Capacity) capacity_error();
                base_[next_size++] = overflow;
            }
            for (rstd::size_t i = next_size - (overflow != rstd::uint32_t(0));
                 i > digits + rstd::size_t(1);
                 --i) {
                auto const index = i - rstd::size_t(1);
                base_[index]     = (base_[index] << shift) |
                                   (base_[index - rstd::size_t(1)] >> (rstd::uint32_t(32) - shift));
            }
            base_[digits] <<= shift;
        }
        size_ = next_size;
    }

    void mul_pow5(rstd::size_t exponent) {
        while (exponent >= rstd::size_t(13)) {
            mul_small(rstd::uint32_t(1'220'703'125));
            exponent -= rstd::size_t(13);
        }
        rstd::uint32_t rest = 1;
        while (exponent-- != rstd::size_t(0)) rest *= rstd::uint32_t(5);
        mul_small(rest);
    }

    void mul_digits(const rstd::uint32_t* other, rstd::size_t other_len) {
        rstd::uint32_t result[Capacity] {};
        rstd::size_t   result_size = 0;
        for (rstd::size_t i = 0; i < size_; ++i) {
            if (base_[i] == rstd::uint32_t(0)) continue;
            rstd::uint64_t carry = 0;
            for (rstd::size_t j = 0; j < other_len; ++j) {
                if (i + j >= Capacity) capacity_error();
                auto const value =
                    static_cast<rstd::uint64_t>(base_[i]) * other[j] + result[i + j] + carry;
                result[i + j] = static_cast<rstd::uint32_t>(value);
                carry         = value >> rstd::uint32_t(32);
            }
            auto size = other_len;
            if (carry != rstd::uint64_t(0)) {
                if (i + size >= Capacity) capacity_error();
                result[i + size++] = static_cast<rstd::uint32_t>(carry);
            }
            if (result_size < i + size) result_size = i + size;
        }
        for (rstd::size_t i = 0; i < Capacity; ++i) base_[i] = result[i];
        size_ = result_size;
    }

    auto div_rem_small(rstd::uint32_t other) -> rstd::uint32_t {
        rstd::uint64_t remainder = 0;
        for (rstd::size_t i = size_; i != rstd::size_t(0); --i) {
            auto const value = (remainder << rstd::uint32_t(32)) | base_[i - rstd::size_t(1)];
            base_[i - rstd::size_t(1)] = static_cast<rstd::uint32_t>(value / other);
            remainder                  = value % other;
        }
        return static_cast<rstd::uint32_t>(remainder);
    }
};

} // namespace rstd::num::bignum
