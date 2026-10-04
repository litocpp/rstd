import rstd.core;

using namespace rstd::prelude;
namespace random = rstd::random;

struct CoreSeed {
    array<u32, 2> material { u32(123), u32(456) };
    void generate(mut_ref<u32[]> out) { random::generate_seed_sequence(material.as_slice(), out); }
};

struct CustomSource {
    u8 byte { u8(7) };
};
template<>
struct rstd::Impl<random::Rng, CustomSource> : rstd::ImplBase<CustomSource> {
    using Error = rstd::convert::Infallible;
    constexpr auto fill_bytes(mut_ref<u8[]> output) -> Result<empty, Error> {
        for (usize i {}; i < output.len(); ++i) output[i] = this->self().byte;
        return Ok(empty {});
    }
};

constexpr auto custom_source() -> bool {
    CustomSource source;
    return random::sample(random::Standard<u8> {}, source).unwrap() == u8(7);
}
static_assert(custom_source());

auto main() -> int {
    CoreSeed sequence;
    auto     engine = random::Mt19937_64::from_seed_sequence(sequence);
    auto     copy   = rstd::as<rstd::clone::Clone>(engine).clone();
    auto     dist   = random::UniformInt<u128>::make(u128(3), u128::MAX).unwrap();
    auto     a      = random::sample(dist, engine).unwrap();
    auto     b      = random::sample(dist, copy).unwrap();
    return custom_source() && a == b && engine == copy ? 0 : 1;
}
