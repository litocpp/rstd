module;
#include <rstd/macro.hpp>
module rstd_benches;
import rstd;
import rstd.bench;
using namespace rstd::prelude;
using namespace rstd::literals;
namespace bench = rstd::bench;

enum class IoCase
{
    Cursor,
    Dynamic,
    ShortInitialized,
    ShortUninit,
    BufferedSmall,
    BufferedLarge,
    WriterSmall,
    WriterBoundary,
    FileHint,
    FileUnknown
};
struct IoCounts {
    u64 reads {}, initialized {}, copied {};
};
struct IoShortReader {
    slice<u8> bytes;
    usize     position {};
    bool      initialize {};
    IoCounts* counts {};
};
namespace rstd
{
template<>
struct Impl<io::Read, IoShortReader> : DefaultInImpl<io::Read, IoShortReader> {
    auto read_buf(io::BorrowedCursor target) -> io::Result<empty> {
        auto& self = this->self();
        if (self.counts) ++self.counts->reads;
        if (self.initialize) {
            if (self.counts)
                self.counts->initialized +=
                    u64((target.capacity() - target.initialized_len()).to_primitive());
            (void)target.ensure_init();
        }
        auto count = min(min(target.capacity(), self.bytes.len() - self.position), usize(17));
        if (count != usize())
            target.append(slice<u8>::from_raw_parts(
                self.bytes.as_raw_ptr() + self.position.to_primitive(), count));
        self.position += count;
        if (self.counts) self.counts->copied += u64(count.to_primitive());
        return Ok(empty {});
    }
};
} // namespace rstd

template<typename A>
struct ReadToEndSession {
    Vec<u8>                   source;
    A                         allocator;
    IoCase                    kind;
    bool                      reserved;
    bool                      diagnostic {};
    IoCounts                  counts;
    usize                     result_capacity {};
    Option<rstd::fs::TempDir> directory;
    Option<rstd::fs::File>    file;
    ReadToEndSession(usize size, A allocator, IoCase kind = IoCase::Cursor, bool reserved = false)
        : allocator(rstd::move(allocator)), kind(kind), reserved(reserved) {
        source.resize(size, u8(42));
        if (kind == IoCase::FileHint || kind == IoCase::FileUnknown) {
            directory = Some(rstd::fs::TempDir::make("rstd-io-bench"_str).unwrap());
            auto root = rstd::path::PathBuf::from(directory->path().as_os_str().to_os_string());
            auto path = root.join("bytes"_str);
            rstd::fs::write(path.as_path(), source.as_slice()).unwrap();
            file = Some(rstd::fs::File::open(path.as_path()).unwrap());
        }
    }
    auto operation() {
        auto reader = rstd::io::Cursor(rstd::hint::black_box(source.as_slice()));
        auto output = Vec<u8, A>::new_in(rstd::alloc::clone_allocator(allocator));
        if (reserved) output.try_reserve_exact(source.len()).unwrap();
        if (kind == IoCase::Cursor) {
            rstd::io::read_to_end(reader, output).unwrap();
        } else if (kind == IoCase::Dynamic) {
            auto dynamic  = rstd::hint::black_box(rstd::dyn<rstd::io::Read>::from_ref(reader));
            auto borrowed = *dynamic;
            rstd::io::read_to_end(borrowed, output).unwrap();
        } else if (kind == IoCase::ShortInitialized || kind == IoCase::ShortUninit) {
            IoShortReader short_reader { source.as_slice(),
                                         usize(),
                                         kind == IoCase::ShortInitialized,
                                         diagnostic ? &counts : nullptr };
            rstd::io::read_to_end(short_reader, output).unwrap();
        } else if (kind == IoCase::FileHint || kind == IoCase::FileUnknown) {
            file->seek(rstd::io::SeekFrom::from_start(u64())).unwrap();
            if (kind == IoCase::FileHint)
                rstd::io::read_to_end(*file, output).unwrap();
            else
                rstd::io::default_read_to_end(*file, output).unwrap();
        } else if (kind == IoCase::BufferedSmall || kind == IoCase::BufferedLarge) {
            auto buffered = rstd::io::BufReader<decltype(reader), A>::make_in(
                rstd::move(reader), rstd::alloc::clone_allocator(allocator), usize(8192));
            output.try_reserve_exact(source.len()).unwrap();
            auto chunk = kind == IoCase::BufferedSmall ? usize(16) : usize(32768);
            while (output.len() != source.len()) {
                auto target = rstd::io::BorrowedBuf::from_uninit(
                    rstd::slice_::split_at_mut(output.spare_capacity_mut().as_uninit(),
                                               rstd::min(chunk, source.len() - output.len()))
                        .template get<0>());
                rstd::as<rstd::io::Read>(buffered).read_buf(target.unfilled()).unwrap();
                if (target.len() == usize()) rstd::panic { "benchmark unexpected eof" };
                output.set_len_unchecked(output.len() + target.len());
            }
        } else {
            auto sink   = rstd::io::Cursor(rstd::move(output));
            auto writer = rstd::io::BufWriter<decltype(sink), A>::make_in(
                rstd::move(sink), rstd::alloc::clone_allocator(allocator), usize(8192));
            auto chunk = kind == IoCase::WriterSmall ? usize(16) : usize(8191);
            for (usize pos; pos < source.len();) {
                auto count = rstd::min(chunk, source.len() - pos);
                rstd::io::write_all(
                    writer,
                    rstd::slice<u8>::from_raw_parts(source.data() + pos.to_primitive(), count))
                    .unwrap();
                pos += count;
            }
            rstd::as<rstd::io::Write>(writer).flush().unwrap();
            output = rstd::move(writer).into_inner().into_inner();
        }
        result_capacity = output.capacity();
        return output;
    }
    auto check() -> Result<empty, String> {
        auto output = operation();
        if (output.as_slice() != source.as_slice()) return Err("read contents differ"_Str);
        return Ok(empty {});
    }
    auto work() const -> bench::RunConfig {
        return { .items_per_iteration = u64(1),
                 .bytes_per_iteration = u64(source.len().to_primitive()) };
    }
    auto run(bench::Bench& engine, ref<str> name)
        -> Result<bench::BenchmarkResult, bench::BenchError> {
        auto workload = bench::repeated(
            [this] {
                return operation();
            },
            work());
        return workload.run(engine, name);
    }
    auto run(u64 iterations) -> Result<bench::DiagnosticMeasurement, String> {
        diagnostic = true;
        counts     = {};
        for (u64 i; i < iterations; ++i) {
            auto value = operation();
            rstd::hint::black_box(value);
        }
        bench::DiagnosticMeasurement result;
        result.work = work();
        auto metric = [&](ref<str> name, u64 value, bench::DiagnosticUnit unit) {
            result.metrics.push(bench::DiagnosticMetric {
                String::make(name),
                unit,
                bench::MetricDirection::Lower,
                "operation"_Str,
                String::make(name == "result_capacity"_str ? "observed final Vec capacity"_str
                                                           : "reader instrumentation"_str),
                bench::DiagnosticValue::Count(value) });
        };
        if (kind == IoCase::ShortInitialized || kind == IoCase::ShortUninit) {
            metric("read_calls"_str, counts.reads, bench::DiagnosticUnit::Count);
            metric("initialized_bytes"_str, counts.initialized, bench::DiagnosticUnit::Bytes);
            metric("reader_copied_bytes"_str, counts.copied, bench::DiagnosticUnit::Bytes);
        }
        metric("result_capacity"_str,
               u64(result_capacity.to_primitive()),
               bench::DiagnosticUnit::Bytes);
        return Ok(rstd::move(result));
    }
    auto finish() -> Result<empty, String> { return Ok(empty {}); }
};
struct ReadToEndFactory {
    usize  size;
    IoCase kind { IoCase::Cursor };
    bool   reserved {};
    auto   prepare() {
        using Session = ReadToEndSession<::alloc::Global>;
        return Result<Session, String>(Ok(Session(size, {}, kind, reserved)));
    }
    auto prepare(bench::DiagnosticContext& context) {
        using Session = ReadToEndSession<rstd::alloc::TrackingAllocator<>>;
        return Result<Session, String>(Ok(Session(size, context.allocator(), kind, reserved)));
    }
};
struct TrackedReadToEndFactory : ReadToEndFactory {
    using ReadToEndFactory::prepare;
    auto prepare() {
        using Allocator = rstd::alloc::SharedTrackingAllocator<>;
        using Session   = ReadToEndSession<Allocator>;
        return Result<Session, String>(Ok(Session(size, Allocator::make(), kind, reserved)));
    }
};
auto rstd_bench::register_io(bench::Suite& suite) -> Result<empty, String> {
    constexpr rstd::size_t sizes[] { 0, 64, 65536, 1048576 };
    for (auto size : sizes) {
        auto descriptor = make_descriptor("io_read_to_end"_str,
                                          rstd::format("bytes_{}", size),
                                          u64(3),
                                          parameter("bytes"_str, u64(size)));
        descriptor.parameters.push(parameter(
            "reader_bytes"_str, u64(sizeof(rstd::io::BufReader<rstd::io::Cursor<slice<u8>>>))));
        descriptor.parameters.push(
            parameter("writer_bytes"_str, u64(sizeof(rstd::io::BufWriter<rstd::io::Sink>))));
        descriptor.parameters.push(
            parameter("handle_bytes"_str, u64(sizeof(rstd::io::ReadSeekHandle))));
        auto id = descriptor.id.clone();
        rstd_try(suite.add(rstd::move(descriptor), ReadToEndFactory { usize(size) }));
        rstd_try(suite.add_diagnostic(id, ReadToEndFactory { usize(size) }));
    }
    for (auto size : sizes) {
        for (int variant = 0; variant < 3; ++variant) {
            auto kind     = variant == 0 ? IoCase::Cursor : IoCase::Dynamic;
            bool reserved = variant != 1;
            auto name     = rstd::format(
                "{}_{}_{}", variant == 0 ? "static" : "dyn", reserved ? "reserved" : "fresh", size);
            auto descriptor = make_descriptor(
                "io_collection"_str, name, u64(3), parameter("bytes"_str, u64(size)));
            auto             id = descriptor.id.clone();
            ReadToEndFactory factory { usize(size), kind, reserved };
            rstd_try(suite.add(rstd::move(descriptor), factory));
            rstd_try(suite.add_diagnostic(id, factory));
        }
    }
    for (auto size : sizes) {
        for (int variant = 0; variant < 4; ++variant) {
            auto kind     = variant < 2 ? IoCase::Cursor : IoCase::Dynamic;
            bool reserved = variant % 2 != 0;
            auto descriptor =
                make_descriptor("io_custom_allocator"_str,
                                rstd::format("{}_{}_{}",
                                             variant < 2 ? "static" : "dyn",
                                             reserved ? "reserved" : "fresh",
                                             size),
                                u64(3),
                                parameter("bytes"_str, u64(size)),
                                parameter("timing_allocator"_str, "SharedTrackingAllocator"_str));
            auto                    id = descriptor.id.clone();
            TrackedReadToEndFactory factory { ReadToEndFactory { usize(size), kind, reserved } };
            rstd_try(suite.add(rstd::move(descriptor), factory));
            rstd_try(suite.add_diagnostic(id, factory));
        }
    }
    struct Case {
        IoCase   kind;
        ref<str> name;
    };
    const Case cases[] { { IoCase::ShortInitialized, "short_initialized"_str },
                         { IoCase::ShortUninit, "short_uninit"_str },
                         { IoCase::BufferedSmall, "buffered_small"_str },
                         { IoCase::BufferedLarge, "buffered_large"_str },
                         { IoCase::WriterSmall, "writer_small"_str },
                         { IoCase::WriterBoundary, "writer_boundary"_str },
                         { IoCase::FileHint, "file_hint"_str },
                         { IoCase::FileUnknown, "file_unknown"_str } };
    for (auto item : cases) {
        auto descriptor = make_descriptor(
            "io_buffers"_str, item.name, u64(3), parameter("bytes"_str, u64(65536)));
        auto             id = descriptor.id.clone();
        ReadToEndFactory factory { usize(65536), item.kind };
        rstd_try(suite.add(rstd::move(descriptor), factory));
        rstd_try(suite.add_diagnostic(id, factory));
    }
    return Ok(empty {});
}
