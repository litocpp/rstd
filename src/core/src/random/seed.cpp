module rstd.core;
import :random.seed;
import :prelude;

using namespace rstd::prelude;

auto seed_mix(u32 value) -> u32 {
    return value ^ (value >> u64(27));
}

void rstd::random::generate_seed_sequence(ref<u32[]> input, mut_ref<u32[]> output) {
    auto n = output.len();
    if (n == usize()) return;
    for (auto& value : output) value = u32(0x8b8b8b8b);
    auto t = n >= usize(623)  ? usize(11)
             : n >= usize(68) ? usize(7)
             : n >= usize(39) ? usize(5)
             : n >= usize(7)  ? usize(3)
                              : (n - usize(1)) / usize(2);
    auto p = (n - t) / usize(2);
    auto q = p + t;
    auto s = input.len();
    auto m = n > s + usize(1) ? n : s + usize(1);
    auto first =
        seed_mix(output[usize()] ^ output[p] ^ output[n - usize(1)]).wrapping_mul(u32(1664525));
    output[p]       = output[p].wrapping_add(first);
    auto second     = first.wrapping_add(u32(s.to_primitive()));
    output[q]       = output[q].wrapping_add(second);
    output[usize()] = second;
    for (usize k(1); k < m; ++k) {
        auto at    = k % n;
        auto left  = (k + p) % n;
        auto right = (k + q) % n;
        auto a     = seed_mix(output[at] ^ output[left] ^ output[(k - usize(1)) % n])
                         .wrapping_mul(u32(1664525));
        auto b     = a.wrapping_add(u32(at.to_primitive()));
        if (k <= s) b = b.wrapping_add(input[k - usize(1)]);
        output[left]  = output[left].wrapping_add(a);
        output[right] = output[right].wrapping_add(b);
        output[at]    = b;
    }
    for (usize k = m; k < m + n; ++k) {
        auto at    = k % n;
        auto left  = (k + p) % n;
        auto right = (k + q) % n;
        auto a =
            seed_mix(output[at].wrapping_add(output[left]).wrapping_add(output[(k - usize(1)) % n]))
                .wrapping_mul(u32(1566083941));
        auto b = a.wrapping_sub(u32(at.to_primitive()));
        output[left] ^= a;
        output[right] ^= b;
        output[at] = b;
    }
}
