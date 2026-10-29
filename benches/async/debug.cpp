import rstd;
import rstd.bench;
using namespace rstd;
using namespace rstd::prelude;
using namespace rstd::literals;
namespace dbg   = async::debug;
namespace bench = rstd::bench;

auto snapshot(const dbg::Handle& handle) -> dbg::Snapshot {
    for (int attempt = 0; attempt < 10000; ++attempt) {
        auto s = handle.snapshot();
        if (s.is_ok() || s.unwrap_err() != dbg::Error::Busy) return rstd::move(s).unwrap();
        thread::yield_now();
    }
    return handle.snapshot().unwrap();
}
auto sample(const dbg::Handle& handle, bool repeated, bool query) -> async::coro<f64> {
    bench::BenchConfig config;
    config.epochs                 = usize(7);
    config.exact_epoch_iterations = Some(u64(32));
    config.warmup_iterations      = u64(8);
    auto           engine         = bench::Bench::new_(config);
    dbg::WaitGuard guard;
    if (repeated) guard.begin("micro_wait"_str);
    auto measured = engine
                        .run("wait"_str,
                             [&] {
                                 if (repeated) {
                                     guard.begin("micro_wait"_str);
                                     hint::black_box(guard);
                                 } else {
                                     dbg::WaitGuard once;
                                     once.begin("micro_wait"_str);
                                     if (query) {
                                         hint::black_box(snapshot(handle));
                                     }
                                 }
                             })
                        .unwrap();
    guard.reset();
    co_return measured.summary().median_ns_per_unit;
}
auto waits() -> async::coro<int> {
    for (int i = 0; i < 32; ++i) {
        dbg::WaitGuard guard;
        guard.begin("parallel_wait"_str);
    }
    co_return 32;
}
auto valid(const dbg::Snapshot& view) -> bool {
    if (view.omitted || view.dropped_events) return false;
    for (const auto& record : view.records)
        if (record.kind == dbg::Kind::Wait && ! record.terminal) return false;
    return true;
}
auto main() -> int {
    for (int mode = 0; mode < 4; ++mode) {
        dbg::Options options;
        options.max_closed         = 32;
        options.max_pending_events = 32768;
        options.max_events         = mode == 2 ? 8 : 8192;
        auto rt    = async::RuntimeBuilder::current_thread().enable_debug(options).build().unwrap();
        auto h     = rt.debug_handle().unwrap();
        auto value = rt.block_on(sample(h, mode == 0, mode == 3));
        auto s     = snapshot(h);
        if (! valid(s)) return 1;
        io::println("mode={} median_ns={} omitted={} dropped={} cursor={}",
                    mode,
                    value,
                    s.omitted,
                    s.dropped_events,
                    s.cursor);
    }
    dbg::Options options;
    options.max_closed         = 32;
    options.max_pending_events = 32768;
    options.max_events         = 256;
    auto               rt      = async::RuntimeBuilder::multi_thread()
                                     .worker_threads(usize(2))
                                     .enable_debug(options)
                                     .build()
                                     .unwrap();
    auto               h       = rt.debug_handle().unwrap();
    bench::BenchConfig config;
    config.epochs                 = usize(7);
    config.exact_epoch_iterations = Some(u64(8));
    config.warmup_iterations      = u64(2);
    auto engine                   = bench::Bench::new_(config);
    int  total                    = 0;
    auto result                   = engine
                                        .run("parallel"_str,
                                             [&] {
                               auto a = rt.spawn(waits());
                               auto b = rt.spawn(waits());
                               total += rt.block_on(rstd::move(a)).unwrap();
                               total += rt.block_on(rstd::move(b)).unwrap();
                                             },
                                             { .items_per_iteration = u64(64) })
                                        .unwrap();
    auto s                        = snapshot(h);
    if (! valid(s) || total != 3712) return 1;
    io::println("mode=4 median_ns={} omitted={} dropped={} cursor={} total={}",
                result.summary().median_ns_per_unit,
                s.omitted,
                s.dropped_events,
                s.cursor,
                total);
}
