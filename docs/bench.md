# rstd.bench

`rstd.bench` provides two explicit CPU measurement facilities:

- `Bench` repeatedly executes one operation and returns owned epoch measurements for a microbenchmark.
- `probe` records real application spans into per-thread buffers and aggregates them at an explicit boundary.

They share clocks, durations, and statistics, but not a runtime object. A loading function or rendering pass is never repeated by the probe facility.

## Linking and importing

Link the `rstd::bench` CMake target and import the module:

```cmake
target_link_libraries(my_target PRIVATE rstd::bench)
```

```cpp
import rstd.bench;

using namespace rstd::prelude;
```

Consumers must compile as standard C++20 with compiler extensions disabled so
their module language dialect matches rstd:

```cmake
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
```

The library is built independently of the `RSTD_BUILD_BENCHMARKS` option. That option only controls the repository's `rstd_bench` executable.

## Microbenchmarks

```cpp
using namespace rstd::bench;

auto config = BenchConfig {};
config.counter_mode = CounterMode::Auto();

auto bench = Bench::new_(rstd::move(config));
auto result = bench.run("vec push", [] {
    auto values = Vec<u64>::make();
    values.push(u64(42));
    hint::black_box(values);
});

if (result.is_ok()) {
    auto measured = rstd::move(result).unwrap_unchecked();
    auto summary = measured.summary();
    hint::black_box(summary.median_ns_per_unit);
}
```

Each `EpochMeasurement` owns the total elapsed duration, iteration count, and optional counters for one epoch. `BenchmarkSummary` is derived from those measurements and includes median, mean, min/max, MdAPE, throughput, and available counter rates.

The operation must have an observable effect or use `hint::black_box`. Warmup calls are not included in measurements. Set `exact_epoch_iterations` for deterministic smoke tests; leave it empty for clock-resolution-based adaptive epochs.

Counter modes are explicit:

- `Disabled` performs wall-clock measurement only.
- `Auto` records counters when Linux perf events are available and otherwise returns a successful result with `CounterAvailability::Unavailable`.
- `Required` returns `BenchError::CounterUnavailable` when counters cannot be opened or read.

Counters describe the thread running `Bench::run`, not work dispatched to other threads. Fixed perf measurement overhead is calibrated for all counters; the loop contribution is subtracted only from retired instructions.

## Runtime probes

Register labels before entering the measured path and freeze them into an owned schema:

```cpp
using namespace rstd::bench::probe;

auto registry = ProbeRegistry::new_();
auto load_mesh = registry.register_probe("load.mesh").unwrap();
auto visibility = registry.register_probe("render.visibility").unwrap();

auto schema = rstd::move(registry).freeze();
auto session = ProbeSession::new_(schema.clone());
auto recorder = session.recorder(RecorderConfig {
    .sample_capacity = usize(4096),
    .active_span_capacity = usize(64),
    .overflow = OverflowPolicy::DropNewest(),
    .thread_policy = ThreadPolicy::Verify(),
});

{
    auto span = recorder.span(load_mesh);
    load_meshes();
}

recorder.begin_frame(u64(42)).unwrap();
{
    auto span = recorder.span(visibility);
    render_visibility();
}
auto batch = recorder.end_frame().unwrap();
```

A recorder is thread-affine and has no global lock. Create one recorder per application thread from the same session, then move completed batches to a collector. Recorders from one session share the schema and clock origin.

`SpanGuard` is move-only. Destruction and explicit `finish()` use the same completion path and end a span at most once. Nested samples carry their parent sequence, allowing the collector to calculate inclusive and exclusive duration without parsing an event stream.

With `DropNewest`, completion does not grow the sample buffer. Overflow increments `dropped_samples` and never leaves an incomplete begin event. `Grow` is intended for offline inspection where allocation is acceptable. `ThreadPolicy::Verify` checks each span boundary; `Unchecked` explicitly removes that check.

Invalid IDs, wrong-thread access, active-span overflow, non-LIFO completion, and frame state errors use `ProbeDiagnostic`. Diagnostics that occur in a guard path are sticky and are returned from the next explicit `finish`, `drain`, or frame boundary. Destructors do not throw or write logs.

```cpp
auto collector = ProbeCollector::new_(schema.clone());
collector.ingest(batch).unwrap();
recorder.recycle(rstd::move(batch)).unwrap();
auto report = rstd::move(collector).finish();

auto output = rstd::io::stdout();
write_text(output, report).unwrap();
```

`ProbeCollector` consumes structured samples and exposes overall, per-thread, and per-frame summaries together with dropped counts and diagnostics. `write_text` is an explicit cold-path operation over `io::Write`; the module has no default output, background thread, static registration, or logging dependency.

`recycle` returns a batch's sample storage to its originating recorder. It is optional, but using it at stable frame boundaries avoids repeated allocation once capacity is sufficient.
