module;
#include <rstd/macro.hpp>
module rstd_benches;
import rstd;
import rstd.cppstd;
import rstd.bench;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace rstd_bench;
namespace bench  = rstd::bench;
namespace random = rstd::random;

enum class RandomWork
{
    Seed32,
    Seed64,
    Next32,
    Next64,
    Fill,
    Integer,
    Unit,
    Real,
    Bernoulli,
    Normal,
    ColdNormal,
    Shuffle,
    System
};

template<bool Cppstd, RandomWork Work, rstd::size_t Size = 256>
struct RandomSession {
    using Engine32 = rstd::mtp::cond<Cppstd, std::mt19937, random::Mt19937>;
    using Engine64 = rstd::mtp::cond<Cppstd, std::mt19937_64, random::Mt19937_64>;
    Engine32 small;
    Engine64 engine;
    using Integer = rstd::mtp::
        cond<Cppstd, std::uniform_int_distribution<rstd::uint64_t>, random::UniformInt<u64>>;
    using Real =
        rstd::mtp::cond<Cppstd, std::uniform_real_distribution<double>, random::UniformReal<f64>>;
    using Bernoulli = rstd::mtp::cond<Cppstd, std::bernoulli_distribution, random::Bernoulli>;
    using Normal = rstd::mtp::cond<Cppstd, std::normal_distribution<double>, random::Normal<f64>>;
    static auto make_integer() {
        if constexpr (Cppstd)
            return Integer(1, 6);
        else
            return Integer::make(u64(1), u64(6)).unwrap();
    }
    static auto make_real() {
        if constexpr (Cppstd)
            return Real(Work == RandomWork::Unit ? 0 : -1, 1);
        else
            return Real::make(f64(-1), f64(1)).unwrap();
    }
    static auto make_bernoulli() {
        if constexpr (Cppstd)
            return Bernoulli(0.25);
        else
            return Bernoulli::make(f64(0.25)).unwrap();
    }
    static auto make_normal() {
        if constexpr (Cppstd)
            return Normal(0, 1);
        else
            return Normal::make(f64(), f64(1)).unwrap();
    }
    Integer          integer { make_integer() };
    Real             real { make_real() };
    Bernoulli        bernoulli { make_bernoulli() };
    Normal           normal { make_normal() };
    array<u8, Size>  bytes {};
    array<u32, Size> values {};
    bool             succeeded { true };

    RandomSession() {
        if constexpr (Cppstd) {
            small.seed(123);
            engine.seed(123);
        } else {
            small.seed(u32(123));
            engine.seed(u64(123));
        }
        for (usize i {}; i < values.len(); ++i) values[i] = u32(i.to_primitive());
    }
    auto operation() {
        if constexpr (Work == RandomWork::Seed32) {
            if constexpr (Cppstd)
                return Engine32(rstd::hint::black_box(123u));
            else
                return Engine32::from_seed(rstd::hint::black_box(u32(123)));
        } else if constexpr (Work == RandomWork::Seed64) {
            if constexpr (Cppstd)
                return Engine64(rstd::hint::black_box(123ULL));
            else
                return Engine64::from_seed(rstd::hint::black_box(u64(123)));
        } else if constexpr (Work == RandomWork::Next32) {
            if constexpr (Cppstd)
                return small();
            else
                return small.next();
        } else if constexpr (Work == RandomWork::Next64) {
            if constexpr (Cppstd)
                return engine();
            else
                return engine.next();
        } else if constexpr (Work == RandomWork::Integer) {
            if constexpr (Cppstd)
                return integer(engine);
            else
                return random::sample(integer, engine).unwrap();
        } else if constexpr (Work == RandomWork::Unit) {
            if constexpr (Cppstd)
                return real(engine);
            else
                return random::sample(random::Standard<f64> {}, engine).unwrap();
        } else if constexpr (Work == RandomWork::Real) {
            if constexpr (Cppstd)
                return real(engine);
            else
                return random::sample(real, engine).unwrap();
        } else if constexpr (Work == RandomWork::Bernoulli) {
            if constexpr (Cppstd)
                return bernoulli(engine);
            else
                return random::sample(bernoulli, engine).unwrap();
        } else if constexpr (Work == RandomWork::Normal || Work == RandomWork::ColdNormal) {
            if constexpr (Work == RandomWork::ColdNormal) normal.reset();
            if constexpr (Cppstd)
                return normal(engine);
            else
                return random::sample(normal, engine).unwrap();
        } else if constexpr (Work == RandomWork::Shuffle) {
            if constexpr (Cppstd)
                std::shuffle(values.data(), values.data() + Size, engine);
            else
                random::shuffle(values.as_mut_slice(), engine).unwrap();
            return values.as_slice();
        } else if constexpr (Work == RandomWork::System) {
            succeeded = random::fill_secure(bytes.as_mut_slice()).is_ok() && succeeded;
            return bytes.as_slice();
        } else {
            engine.fill_bytes(bytes.as_mut_slice()).unwrap();
            return bytes.as_slice();
        }
    }
    static auto work() -> bench::RunConfig {
        if constexpr (Work == RandomWork::Fill || Work == RandomWork::System)
            return { .bytes_per_iteration = u64(Size) };
        else if constexpr (Work == RandomWork::Shuffle)
            return { .items_per_iteration = u64(Size) };
        else
            return { .items_per_iteration = u64(1) };
    }
    auto check() -> Result<empty, String> {
        if (! succeeded) return Err("system random source failed"_Str);
        if constexpr (Work == RandomWork::Shuffle) {
            array<bool, Size> seen {};
            for (auto value : values) {
                auto i = usize(value.to_primitive());
                if (i >= usize(Size) || seen[i]) return Err("shuffle permutation mismatch"_Str);
                seen[i] = true;
            }
        }
        return Ok(empty {});
    }
    auto run(bench::Bench& runner, ref<str> name)
        -> Result<bench::BenchmarkResult, bench::BenchError> {
        auto workload = bench::repeated(
            [this] {
                return operation();
            },
            work());
        return workload.run(runner, name);
    }
    auto run(u64 iterations) -> Result<bench::DiagnosticMeasurement, String> {
        for (u64 i {}; i < iterations; ++i) rstd::hint::black_box(operation());
        bench::DiagnosticMeasurement result;
        result.work = work();
        result.allocation_unavailable =
            "fixed storage; no allocator path (source audit, not tracked)"_Str;
        result.metrics.push(
            { "engine_bytes"_Str,
              bench::DiagnosticUnit::Bytes,
              bench::MetricDirection::Neutral,
              "engine"_Str,
              "sizeof"_Str,
              bench::DiagnosticValue::Count(u64(
                  Work == RandomWork::Next32 || Work == RandomWork::Seed32 ? sizeof(Engine32)
                                                                           : sizeof(Engine64))) });
        result.metrics.push({ "normal_bytes"_Str,
                              bench::DiagnosticUnit::Bytes,
                              bench::MetricDirection::Neutral,
                              "distribution"_Str,
                              "sizeof"_Str,
                              bench::DiagnosticValue::Count(u64(sizeof(Normal))) });
        return Ok(rstd::move(result));
    }
    auto finish() -> Result<empty, String> { return check(); }
};

template<bool Cppstd, RandomWork Work, rstd::size_t Size = 256>
struct RandomFactory {
    auto prepare() {
        return Result<RandomSession<Cppstd, Work, Size>, String>(
            Ok(RandomSession<Cppstd, Work, Size> {}));
    }
    auto prepare(bench::DiagnosticContext&) { return prepare(); }
};

template<bool Cppstd, RandomWork Work, rstd::size_t Size = 256>
auto add_random_case(bench::Suite& suite, ref<str> name) -> Result<empty, String> {
    auto full       = Cppstd ? rstd::format("{}/cppstd", name) : String::make(name);
    auto descriptor = make_descriptor("random"_str, full.as_str(), u64(1000));
    if constexpr (Work != RandomWork::System)
        descriptor.parameters.push(parameter("seed"_str, u64(123)));
    if constexpr (Work == RandomWork::Fill || Work == RandomWork::System ||
                  Work == RandomWork::Shuffle)
        descriptor.parameters.push(parameter("n"_str, u64(Size)));
    descriptor.implementation = Cppstd ? "cppstd"_Str : "rstd"_Str;
    auto id                   = descriptor.id.clone();
    rstd_try(suite.add(rstd::move(descriptor), RandomFactory<Cppstd, Work, Size> {}));
    if constexpr (Work != RandomWork::System)
        rstd_try(suite.add_diagnostic(id.as_str(), RandomFactory<Cppstd, Work, Size> {}));
    return Ok(empty {});
}

template<RandomWork Work>
auto add_random_pair(bench::Suite& suite, ref<str> name) -> Result<empty, String> {
    rstd_try((add_random_case<false, Work>(suite, name)));
    rstd_try((add_random_case<true, Work>(suite, name)));
    return Ok(empty {});
}

auto rstd_bench::register_random(bench::Suite& suite) -> Result<empty, String> {
    rstd_try(add_random_pair<RandomWork::Seed32>(suite, "seed32"_str));
    rstd_try(add_random_pair<RandomWork::Seed64>(suite, "seed64"_str));
    rstd_try(add_random_pair<RandomWork::Next32>(suite, "next32"_str));
    rstd_try(add_random_pair<RandomWork::Next64>(suite, "next64"_str));
    rstd_try(add_random_pair<RandomWork::Integer>(suite, "integer"_str));
    rstd_try(add_random_pair<RandomWork::Real>(suite, "real"_str));
    rstd_try(add_random_pair<RandomWork::Unit>(suite, "unit"_str));
    rstd_try(add_random_pair<RandomWork::Bernoulli>(suite, "bernoulli"_str));
    rstd_try(add_random_pair<RandomWork::Normal>(suite, "normal"_str));
    rstd_try(add_random_pair<RandomWork::ColdNormal>(suite, "normal-cold"_str));
    rstd_try(add_random_pair<RandomWork::Shuffle>(suite, "shuffle/256"_str));
    rstd_try((add_random_case<false, RandomWork::Fill, 16>(suite, "fill/16"_str)));
    rstd_try((add_random_case<false, RandomWork::Fill, 256>(suite, "fill/256"_str)));
    rstd_try((add_random_case<false, RandomWork::Fill, 4096>(suite, "fill/4096"_str)));
    rstd_try((add_random_case<false, RandomWork::System, 256>(suite, "system/256"_str)));
    return Ok(empty {});
}
