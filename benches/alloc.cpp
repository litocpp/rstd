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

enum class VecBulkOperation
{
    Push,
    Slice,
    Append,
    Ordinary,
    Trusted,
    Copied,
    Owning,
    Transform,
    CollectTransform
};

struct VecBulkInput {
    Vec<u8> source;
    Vec<u8> destination;
};

template<VecBulkOperation Operation>
struct VecBulkSession {
    usize   length;
    bool    reserved;
    Vec<u8> source;

    VecBulkSession(usize n, bool reserve): length(n), reserved(reserve) {
        source.resize_with(n, [index = rstd::size_t()]() mutable {
            return u8((index++) % 251);
        });
    }

    auto prepare_input() -> VecBulkInput {
        return { source.clone(), reserved ? Vec<u8>::with_capacity(length) : Vec<u8>() };
    }

    static void append(VecBulkInput& input) {
        auto& source      = input.source;
        auto& destination = input.destination;
        using Extend      = rstd::Impl<rstd::iter::Extend<u8>, Vec<u8>>;
        if constexpr (Operation == VecBulkOperation::Push) {
            for (usize i; i < source.len(); ++i) destination.push(u8(source[i]));
        } else if constexpr (Operation == VecBulkOperation::Slice) {
            destination.extend_from_slice(source.as_slice());
        } else if constexpr (Operation == VecBulkOperation::Append) {
            destination.append(source);
        } else if constexpr (Operation == VecBulkOperation::Ordinary) {
            Extend::extend(destination, source.iter().copied().filter([](u8) {
                return true;
            }));
        } else if constexpr (Operation == VecBulkOperation::Trusted) {
            Extend::extend(destination, source.iter().copied().map([](u8 value) {
                return value;
            }));
        } else if constexpr (Operation == VecBulkOperation::Transform) {
            Extend::extend(destination, source.iter().copied().map([](u8 value) {
                return value ^ u8(42);
            }));
        } else if constexpr (Operation == VecBulkOperation::CollectTransform) {
            destination = source.iter()
                              .copied()
                              .map([](u8 value) {
                                  return value ^ u8(42);
                              })
                              .template collect<Vec<u8>>();
        } else if constexpr (Operation == VecBulkOperation::Copied) {
            Extend::extend(destination, source.iter().copied());
        } else {
            Extend::extend(destination, rstd::move(source).into_iter());
        }
        rstd::hint::black_box(destination.as_slice());
    }

    auto check() -> Result<empty, String> {
        auto input = prepare_input();
        append(input);
        if (input.destination.len() != length) return Err("bulk append length mismatch"_Str);
        for (usize i; i < length; ++i)
            if (u8(input.destination[i]) != ((Operation == VecBulkOperation::Transform ||
                                              Operation == VecBulkOperation::CollectTransform)
                                                 ? u8(source[i]) ^ u8(42)
                                                 : u8(source[i])))
                return Err("bulk append content mismatch"_Str);
        if constexpr (Operation == VecBulkOperation::Append ||
                      Operation == VecBulkOperation::Owning)
            if (! input.source.is_empty()) return Err("bulk append source still owns elements"_Str);
        return Ok(empty {});
    }

    auto run(bench::Bench& engine, ref<str> name)
        -> Result<bench::BenchmarkResult, bench::BenchError> {
        auto workload = bench::batched_ref(
            [this] {
                return prepare_input();
            },
            [](VecBulkInput& input) {
                append(input);
            },
            { .max_items = usize(4) },
            { .items_per_iteration = u64(length.to_primitive()),
              .bytes_per_iteration = u64(length.to_primitive()) });
        return workload.run(engine, name);
    }
    auto finish() -> Result<empty, String> { return Ok(empty {}); }
};

template<VecBulkOperation Operation>
auto register_vec_bulk(bench::Suite& suite, ref<str> operation) -> Result<empty, String> {
    constexpr rstd::size_t lengths[] { 0, 64, 4096, 1048576 };
    for (auto length : lengths) {
        for (int reserved = 0; reserved < 2; ++reserved) {
            if constexpr (Operation == VecBulkOperation::CollectTransform)
                if (reserved != 0) continue;
            auto name = rstd::format(
                "{}_{}_{}", operation, reserved ? "reserved"_str : "growing"_str, length);
            rstd_try(suite.add(
                make_descriptor(
                    "vec_bulk"_str, name.as_str(), u64(8), parameter("bytes"_str, u64(length))),
                bench::factory([length, reserved] {
                    using Session = VecBulkSession<Operation>;
                    return Result<Session, String>(Ok(Session(usize(length), reserved != 0)));
                })));
        }
    }
    return Ok(empty {});
}

enum class OwnershipOperation
{
    Box,
    Arc,
    StrongWeak,
    RetainedWeak,
    VecBox,
    Forward,
    CrossThread
};
struct alignas(256) OwnershipAligned {
    int value;
};

template<typename A>
auto ownership_allocator() -> A {
    if constexpr (rstd::mtp::same_as<A, ::alloc::Global>)
        return {};
    else if constexpr (rstd::mtp::same_as<A, rstd::alloc::SharedAllocator>)
        return A::make(::alloc::Global {});
    else
        return A::make();
}

template<typename A, OwnershipOperation Operation, typename T = int>
struct OwnershipSession {
    A allocator { ownership_allocator<A>() };

    auto operation() {
        if constexpr (Operation == OwnershipOperation::Box) {
            auto value = [&] {
                if constexpr (rstd::mtp::same_as<A, ::alloc::Global>)
                    return Box<T>::make(T { 7 });
                else
                    return Box<T, A>::make_in(rstd::alloc::clone_allocator(allocator), T { 7 });
            }();
            rstd::hint::black_box(value.as_ptr());
        } else if constexpr (Operation == OwnershipOperation::VecBox) {
            auto values = Vec<int, A>::new_in(rstd::alloc::clone_allocator(allocator));
            for (int i = 0; i < 64; ++i) values.push(int(i));
            auto copied = values.clone();
            auto boxed  = copied.into_boxed_slice();
            rstd::hint::black_box(boxed.as_ptr());
        } else if constexpr (Operation == OwnershipOperation::Forward) {
            auto small = rstd::alloc::Layout::array<u8>(usize(16)).unwrap();
            auto large = rstd::alloc::Layout::array<u8>(usize(64)).unwrap();
            auto raw = rstd::as<rstd::alloc::Allocator>(allocator).allocate_zeroed(small).unwrap();
            raw      = rstd::as<rstd::alloc::Allocator>(allocator)
                           .grow_zeroed(raw.pointer, small, large)
                           .unwrap();
            raw      = rstd::as<rstd::alloc::Allocator>(allocator)
                           .shrink(raw.pointer, large, small)
                           .unwrap();
            rstd::hint::black_box(raw.pointer);
            rstd::as<rstd::alloc::Allocator>(allocator).deallocate(raw.pointer, small);
        } else {
            auto value = [&] {
                if constexpr (rstd::mtp::same_as<A, ::alloc::Global>)
                    return rstd::sync::Arc<T>::make(T { 7 });
                else
                    return rstd::sync::Arc<T>::make_in(rstd::alloc::clone_allocator(allocator),
                                                       T { 7 });
            }();
            if constexpr (Operation == OwnershipOperation::StrongWeak) {
                auto copied = value.clone();
                auto weak   = value.downgrade();
                rstd::hint::black_box(copied.as_ptr());
                rstd::hint::black_box(weak.as_ptr());
            } else if constexpr (Operation == OwnershipOperation::RetainedWeak) {
                auto weak = value.downgrade();
                value.reset();
                rstd::hint::black_box(weak.as_ptr());
            } else if constexpr (Operation == OwnershipOperation::CrossThread) {
                auto worker = rstd::thread::spawn([value = rstd::move(value)]() mutable {
                                  value.reset();
                              }).unwrap();
                rstd::move(worker).join().unwrap();
            } else
                rstd::hint::black_box(value.as_ptr());
        }
    }
    auto check() -> Result<empty, String> {
        operation();
        return finish();
    }
    auto run(bench::Bench& engine, ref<str> name)
        -> Result<bench::BenchmarkResult, bench::BenchError> {
        auto workload = bench::repeated(
            [this] {
                operation();
            },
            { .items_per_iteration = u64(1) });
        return workload.run(engine, name);
    }
    auto finish() -> Result<empty, String> {
        if constexpr (requires { allocator.snapshot(); }) {
            if (allocator.snapshot().live_bytes != u64())
                return Err("allocator retained a live allocation"_Str);
        }
        return Ok(empty {});
    }
};

template<typename A, OwnershipOperation Operation, typename T = int>
auto add_ownership_case(bench::Suite& suite, ref<str> name) -> Result<empty, String> {
    return suite.add(make_descriptor("allocator_ownership"_str,
                                     name,
                                     u64(10),
                                     parameter("box_bytes"_str, u64(sizeof(Box<T, A>))),
                                     parameter("arc_bytes"_str, u64(sizeof(rstd::sync::Arc<T>))),
                                     parameter("weak_bytes"_str, u64(sizeof(rstd::sync::Weak<T>)))),
                     bench::factory([] {
                         using Session = OwnershipSession<A, Operation, T>;
                         return Result<Session, String>(Ok(Session {}));
                     }));
}

template<typename A>
auto register_ownership_cases(bench::Suite& suite, ref<str> backend) -> Result<empty, String> {
    rstd_try(
        (add_ownership_case<A, OwnershipOperation::Box>(suite, rstd::format("{}_box", backend))));
    rstd_try(
        (add_ownership_case<A, OwnershipOperation::Arc>(suite, rstd::format("{}_arc", backend))));
    rstd_try((add_ownership_case<A, OwnershipOperation::Box, OwnershipAligned>(
        suite, rstd::format("{}_aligned_box", backend))));
    rstd_try((add_ownership_case<A, OwnershipOperation::Arc, OwnershipAligned>(
        suite, rstd::format("{}_aligned_arc", backend))));
    rstd_try((add_ownership_case<A, OwnershipOperation::StrongWeak>(
        suite, rstd::format("{}_strong_weak", backend))));
    rstd_try((add_ownership_case<A, OwnershipOperation::RetainedWeak>(
        suite, rstd::format("{}_retained_weak", backend))));
    rstd_try((add_ownership_case<A, OwnershipOperation::VecBox>(
        suite, rstd::format("{}_vec_clone_box", backend))));
    rstd_try((add_ownership_case<A, OwnershipOperation::Forward>(
        suite, rstd::format("{}_forward", backend))));
    return add_ownership_case<A, OwnershipOperation::CrossThread>(
        suite, rstd::format("{}_thread_create_release", backend));
}

template<bool Reserved>
struct HashReserveSession {
    usize count;
    auto  operation() {
        auto map = rstd::collections::HashMap<u64, u64>::with_hasher(
            rstd::hash::RandomState(u64(123), u64(456)));
        if constexpr (Reserved) map.reserve(count);
        for (usize i; i < count; ++i) map.insert(u64(i.to_primitive()), u64(i.to_primitive()));
        return map;
    }
    auto check() -> Result<empty, String> {
        auto map = operation();
        if (map.len() != count) return Err("map length differs"_Str);
        for (usize i; i < count; ++i)
            if (*map.get(u64(i.to_primitive())).unwrap() != u64(i.to_primitive()))
                return Err("map contents differ"_Str);
        return Ok(empty {});
    }
    auto run(bench::Bench& engine, ref<str> name)
        -> Result<bench::BenchmarkResult, bench::BenchError> {
        auto workload = bench::repeated(
            [this] {
                return operation();
            },
            { .items_per_iteration = u64(count.to_primitive()) });
        return workload.run(engine, name);
    }
    auto finish() -> Result<empty, String> { return Ok(empty {}); }
};
template<bool Reserved>
auto register_hash_reserve(bench::Suite& suite) -> Result<empty, String> {
    constexpr rstd::size_t counts[] { 64, 4096 };
    for (auto count : counts) {
        auto name = rstd::format("{}_{}", Reserved ? "reserved"_str : "growing"_str, count);
        rstd_try(
            suite.add(make_descriptor("hash_reserve"_str, name, u64(3)), bench::factory([count] {
                          using Session = HashReserveSession<Reserved>;
                          return Result<Session, String>(Ok(Session { usize(count) }));
                      })));
    }
    return Ok(empty {});
}

auto rstd_bench::register_alloc(bench::Suite& suite) -> Result<empty, String> {
    rstd_try(register_hash_reserve<false>(suite));
    rstd_try(register_hash_reserve<true>(suite));
    rstd_try(register_ownership_cases<::alloc::Global>(suite, "global"_str));
    rstd_try(register_ownership_cases<rstd::alloc::SharedTrackingAllocator<>>(
        suite, "shared_tracking"_str));
    rstd_try(register_ownership_cases<rstd::alloc::SharedAllocator>(suite, "shared_dyn"_str));
    rstd_try(register_vec_bulk<VecBulkOperation::Push>(suite, "push"_str));
    rstd_try(register_vec_bulk<VecBulkOperation::Slice>(suite, "slice"_str));
    rstd_try(register_vec_bulk<VecBulkOperation::Append>(suite, "append"_str));
    rstd_try(register_vec_bulk<VecBulkOperation::Ordinary>(suite, "ordinary"_str));
    rstd_try(register_vec_bulk<VecBulkOperation::Trusted>(suite, "trusted"_str));
    rstd_try(register_vec_bulk<VecBulkOperation::Copied>(suite, "copied"_str));
    rstd_try(register_vec_bulk<VecBulkOperation::Owning>(suite, "owning"_str));
    rstd_try(register_vec_bulk<VecBulkOperation::Transform>(suite, "transform"_str));
    rstd_try(register_vec_bulk<VecBulkOperation::CollectTransform>(suite, "collect_transform"_str));
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
