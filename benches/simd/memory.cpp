import rstd;
import rstd.bench;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace rstd::bench;

template<typename T>
struct MemorySession {
    static constexpr rstd::size_t count = 8192;
    static constexpr rstd::size_t lanes = sizeof(T) == 4 ? 4 : 2;
    Vec<T>                        input, output;

    void execute() {
        auto* source      = input.as_slice().as_raw_ptr() + 1;
        auto* destination = output.as_mut_slice().as_raw_ptr() + 1;
        for (rstd::size_t i = 0; i < count; i += lanes) {
            auto value = rstd::simd::Simd<T, lanes>::from_slice(
                slice<T>::from_raw_parts(source + i, usize(lanes)));
            value.copy_to_slice(mut_ref<T[]>::from_raw_parts(destination + i, usize(lanes)));
        }
        rstd::hint::black_box(output.as_slice());
    }
    auto check() -> Result<empty, String> {
        execute();
        for (usize i(1); i <= usize(count); ++i)
            if (input[i] != output[i]) return Err("SIMD memory copy mismatch"_Str);
        if (output[usize()] != T(-1) || output[usize(count + 1)] != T(-1))
            return Err("SIMD memory copy changed sentinel"_Str);
        return Ok(empty {});
    }
    auto finish() -> Result<empty, String> { return check(); }
    auto run(Bench& bench, ref<str> name) -> Result<BenchmarkResult, BenchError> {
        RunConfig config;
        config.unit  = "element"_Str;
        config.batch = f64(count);
        return bench.run(
            name,
            [&] {
                execute();
            },
            rstd::move(config));
    }
};

template<typename T>
struct MemoryFactory {
    auto prepare() -> Result<MemorySession<T>, String> {
        MemorySession<T> session;
        session.input.reserve(usize(session.count + 2));
        session.output.reserve(usize(session.count + 2));
        for (rstd::size_t i = 0; i < session.count + 2; ++i) {
            session.input.push(T(i % 127));
            session.output.push(T(-1));
        }
        return Ok(rstd::move(session));
    }
};

int main(int argc, const char* const* argv) {
    rstd::env::args_init(argc, argv);
    RunnerOptions options;
    options.sampling.epochs            = usize(7);
    options.repeats                    = u64(3);
    options.sampling.warmup_iterations = u64(3);
    Suite suite;
    suite
        .add(CaseDescriptor { .id             = "f32-copy-checked-offset1"_Str,
                              .suite          = "simd-memory"_Str,
                              .revision       = "1"_Str,
                              .implementation = "rstd"_Str },
             MemoryFactory<f32> {})
        .unwrap();
    suite
        .add(CaseDescriptor { .id             = "f64-copy-checked-offset1"_Str,
                              .suite          = "simd-memory"_Str,
                              .revision       = "1"_Str,
                              .implementation = "rstd"_Str },
             MemoryFactory<f64> {})
        .unwrap();
    TextReporter reporter;
    auto         result = Runner(rstd::move(options)).run(suite, reporter);
    return result.is_ok() && result->passed() ? 0 : 1;
}
