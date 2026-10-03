module;
#include <rstd/macro.hpp>

module rstd_benches;
import rstd;
import rstd.bench;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace rstd_bench;
namespace bench = rstd::bench;

struct StringCloneSession {
    String source { "benchmark string payload used by rstd clone measurements"_Str };
    String copied;
    auto   check() -> Result<empty, String> {
        copied = source.clone();
        if (copied != source) return Err("clone precheck failed"_Str);
        return Ok(empty {});
    }
    auto run(bench::Bench& engine, ref<str> name)
        -> Result<bench::BenchmarkResult, bench::BenchError> {
        auto work = bench::repeated(
            [this] {
                rstd::hint::black_box(source.as_str());
                return source.clone();
            },
            { .items_per_iteration = u64(1),
              .bytes_per_iteration = u64(source.len().to_primitive()) });
        return work.run(engine, name);
    }
    auto finish() -> Result<empty, String> { return Ok(empty {}); }
};

template<typename A>
auto push_values(Vec<i32, A>& values) -> void {
    for (i32 value; value < i32(64); value += i32(1)) values.push(i32(value));
}

template<bool Reserved, typename A>
struct VecPushSession {
    A           allocator;
    Vec<i32, A> checked;
    explicit VecPushSession(A alloc): allocator(alloc), checked(Vec<i32, A>::new_in(alloc)) {}
    auto check() -> Result<empty, String> {
        checked      = Vec<i32, A>::with_capacity_in(usize(64), allocator);
        auto& values = checked;
        push_values(values);
        if (values.len() != usize(64)) return Err("push precheck failed"_Str);
        for (usize i; i < values.len(); ++i)
            if (values[i] != i32(i.to_primitive())) return Err("push contents differ"_Str);
        return Ok(empty {});
    }
    auto work() const -> bench::RunConfig {
        return { .items_per_iteration = u64(64), .bytes_per_iteration = u64(64 * sizeof(i32)) };
    }
    auto operation() {
        auto values = Vec<i32, A>::with_capacity_in(usize(64), allocator);
        push_values(values);
        return values;
    }
    auto run(bench::Bench& engine, ref<str> name)
        -> Result<bench::BenchmarkResult, bench::BenchError> {
        if constexpr (Reserved) {
            auto workload = bench::batched_ref(
                [this] {
                    return Vec<i32, A>::with_capacity_in(usize(64), allocator);
                },
                [](Vec<i32, A>& values) {
                    push_values(values);
                },
                {},
                work());
            return workload.run(engine, name);
        } else {
            auto workload = bench::repeated(
                [this] {
                    return operation();
                },
                work());
            return workload.run(engine, name);
        }
    }
    auto run(u64 iterations) -> Result<bench::DiagnosticMeasurement, String>
        requires(! Reserved)
    {
        for (u64 i; i < iterations; ++i) {
            auto values = operation();
            rstd::hint::black_box(values);
        }
        bench::DiagnosticMeasurement result;
        result.work = work();
        return Ok(rstd::move(result));
    }
    auto finish() -> Result<empty, String> { return Ok(empty {}); }
};

template<bool Reserved>
struct VecPushFactory {
    auto prepare() -> Result<VecPushSession<Reserved, ::alloc::Global>, String> {
        return Ok(VecPushSession<Reserved, ::alloc::Global> { {} });
    }
    auto prepare(bench::DiagnosticContext& context)
        requires(! Reserved)
    {
        using Session = VecPushSession<false, rstd::alloc::TrackingAllocator<>>;
        return Result<Session, String>(Ok(Session { context.allocator() }));
    }
};

struct BytesSession {
    rstd::byte                 payload[64] {};
    Option<rstd::bytes::Bytes> checked;
    BytesSession() {
        for (rstd::size_t i = 0; i < 64; ++i)
            payload[i] = rstd::byte { static_cast<rstd::uint8_t>(i) };
    }
    auto operation() {
        auto buffer = rstd::bytes::BytesMut::with_capacity(usize(64));
        buffer.extend_from_slice(
            rstd::hint::black_box(slice<u8>::from_raw_parts(payload, usize(64))));
        return buffer.freeze();
    }
    auto check() -> Result<empty, String> {
        checked           = Some(operation());
        const auto& bytes = *checked;
        if (bytes.len() != usize(64)) return Err("bytes precheck failed"_Str);
        for (usize i; i < bytes.len(); ++i)
            if (bytes[i] != u8(i.to_primitive())) return Err("bytes contents differ"_Str);
        return Ok(empty {});
    }
    auto run(bench::Bench& engine, ref<str> name)
        -> Result<bench::BenchmarkResult, bench::BenchError> {
        auto workload = bench::repeated(
            [this] {
                return operation();
            },
            { .items_per_iteration = u64(1), .bytes_per_iteration = u64(64) });
        return workload.run(engine, name);
    }
    auto finish() -> Result<empty, String> { return Ok(empty {}); }
};

auto rstd_bench::register_alloc(bench::Suite& suite) -> Result<empty, String> {
    rstd_try(suite.add(make_descriptor("alloc"_str, "string_clone_end_to_end"_str, u64(1000)),
                       bench::factory([] {
                           return Result<StringCloneSession, String>(Ok(StringCloneSession {}));
                       })));
    rstd_try(suite.add(
        make_descriptor(
            "alloc"_str, "vec_push_end_to_end"_str, u64(1000), parameter("n"_str, u64(64))),
        VecPushFactory<false> {}));
    rstd_try(suite.add_diagnostic("alloc.vec_push_end_to_end"_str, VecPushFactory<false> {}));
    rstd_try(
        suite.add(make_descriptor(
                      "alloc"_str, "vec_push_reserved"_str, u64(1000), parameter("n"_str, u64(64))),
                  VecPushFactory<true> {}));
    return suite.add(make_descriptor("alloc"_str,
                                     "bytes_extend_freeze_end_to_end"_str,
                                     u64(1000),
                                     parameter("n"_str, u64(64))),
                     bench::factory([] {
                         return Result<BytesSession, String>(Ok(BytesSession {}));
                     }));
}
