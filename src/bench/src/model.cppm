module;
#include <rstd/enum.hpp>

export module rstd.bench:model;
export import :clock;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace rstd::bench
{

class CounterMode final {
    RSTD_ENUM(CounterMode, (Disabled), (Auto), (Required))
};

class CounterAvailability final {
    RSTD_ENUM(CounterAvailability, (Disabled), (Available, (u32 mask;)), (Unavailable, (i32 code;)))
};

class BenchConfigError final {
    RSTD_ENUM(BenchConfigError,
              (ZeroEpochs),
              (ZeroMinIterations),
              (ZeroClockResolutionMultiple),
              (ZeroMaxEpochTime),
              (InvalidEpochRange),
              (InvalidBatch))
};

class BenchError final {
    RSTD_ENUM(BenchError,
              (InvalidConfig, (BenchConfigError reason;)),
              (Clock, (ClockError reason;)),
              (IterationOverflow),
              (OperationOptimizedAway),
              (Collector, (String reason;)),
              (Operation, (String reason;)),
              (CounterUnavailable, (i32 code;)))
};

auto describe_error(const BenchError& error) -> String;

enum class MetricUnit
{
    Count,
    Bytes,
    Nanoseconds
};
enum class MetricAggregation
{
    Sum,
    Maximum,
    Last
};
enum class MetricDirection
{
    Lower,
    Higher,
    Neutral
};

struct MetricSample {
    String            name;
    MetricUnit        unit;
    MetricAggregation aggregation;
    MetricDirection   direction;
    u64               value;
};

auto merge_metrics(Vec<MetricSample>& total, Vec<MetricSample> part) -> Result<empty, BenchError>;

struct CounterSet {
    Option<u64> page_faults;
    Option<u64> cpu_cycles;
    Option<u64> context_switches;
    Option<u64> instructions;
    Option<u64> branch_instructions;
    Option<u64> branch_misses;
};

struct BenchConfig {
    usize          epochs { usize(11) };
    time::Duration min_epoch_time { time::Duration::from_millis(u64(1)) };
    time::Duration max_epoch_time { time::Duration::from_millis(u64(100)) };
    u64            min_epoch_iterations { u64(1) };
    Option<u64>    exact_epoch_iterations { None() };
    u64            warmup_iterations {};
    usize          clock_resolution_multiple { usize(1000) };
    CounterMode    counter_mode { CounterMode::Disabled() };
    u64            jitter_seed { u64(123) };

    auto validate() const noexcept -> Result<empty, BenchConfigError> {
        if (epochs == usize()) return Err(BenchConfigError::ZeroEpochs());
        if (min_epoch_iterations == u64()) return Err(BenchConfigError::ZeroMinIterations());
        if (exact_epoch_iterations.is_some() && *exact_epoch_iterations == u64())
            return Err(BenchConfigError::ZeroMinIterations());
        if (clock_resolution_multiple == usize()) {
            return Err(BenchConfigError::ZeroClockResolutionMultiple());
        }
        if (max_epoch_time.is_zero()) return Err(BenchConfigError::ZeroMaxEpochTime());
        if (min_epoch_time > max_epoch_time) return Err(BenchConfigError::InvalidEpochRange());
        return Ok(empty {});
    }
};

class ElapsedTime {
    f64 ns_;
    explicit ElapsedTime(f64 ns): ns_(ns) {}

public:
    ElapsedTime() = default;
    ElapsedTime(time::Duration duration)
        : ns_(duration.as_nanos() <= u128(9007199254740992ull)
                  ? f64(duration.as_nanos().to_primitive())
                  : f64::INFINITY_) {}
    static auto from_nanoseconds(f64 ns) -> Result<ElapsedTime, BenchError> {
        if (! ns.is_finite() || ns <= f64() || ns > f64(9007199254740992.0))
            return Err(BenchError::Operation("invalid elapsed nanoseconds"_Str));
        return Ok(ElapsedTime(ns));
    }
    auto nanoseconds() const noexcept -> f64 { return ns_; }
    auto is_zero() const noexcept -> bool { return ns_ == f64(); }
    auto valid() const noexcept -> bool {
        return ns_.is_finite() && ns_ > f64() && ns_ <= f64(9007199254740992.0);
    }
};
struct TimingDescriptor {
    String      source;
    String      boundary;
    Option<f64> resolution_ns;
    auto        validate() const -> Result<empty, String> {
        if (source.is_empty() || boundary.is_empty()) return Err("missing timing identity"_Str);
        if (resolution_ns.is_some() && (! resolution_ns->is_finite() || *resolution_ns <= f64()))
            return Err("invalid timing resolution"_Str);
        return Ok(empty {});
    }
};
enum class WorkUnit
{
    Count,
    Bytes,
    Flops
};
struct WorkQuantity {
    String   name;
    WorkUnit unit;
    u64      per_iteration;
    auto     clone() const -> WorkQuantity { return { name.clone(), unit, per_iteration }; }
    void     clone_from(const WorkQuantity& other) { *this = other.clone(); }
};
struct WorkRate {
    String   name;
    WorkUnit unit;
    u64      total;
    f64      per_second;
};

struct RunConfig {
    String            unit { String::make("op"_str) };
    f64               batch { f64(1.0) };
    u64               items_per_iteration {};
    u64               bytes_per_iteration {};
    Vec<WorkQuantity> quantities;

    auto same_work(const RunConfig& other) const -> bool {
        if (unit != other.unit || batch != other.batch ||
            items_per_iteration != other.items_per_iteration ||
            bytes_per_iteration != other.bytes_per_iteration ||
            quantities.len() != other.quantities.len())
            return false;
        for (usize i; i < quantities.len(); ++i) {
            const auto& a = quantities[i];
            const auto& b = other.quantities[i];
            if (a.name != b.name || a.unit != b.unit || a.per_iteration != b.per_iteration)
                return false;
        }
        return true;
    }
    auto validate() const noexcept -> Result<empty, BenchConfigError> {
        if (! batch.is_finite() || batch <= f64() || unit.is_empty())
            return Err(BenchConfigError::InvalidBatch());
        for (usize i; i < quantities.len(); ++i) {
            const auto& q = quantities[i];
            if (q.name.is_empty() || q.name == "items"_str || q.name == "bytes"_str ||
                (q.unit != WorkUnit::Count && q.unit != WorkUnit::Bytes &&
                 q.unit != WorkUnit::Flops))
                return Err(BenchConfigError::InvalidBatch());
            for (usize j; j < i; ++j)
                if (quantities[j].name == q.name) return Err(BenchConfigError::InvalidBatch());
        }
        return Ok(empty {});
    }
};

enum class MeasurementScope
{
    Repeated,
    Batched,
    BatchedRef,
    External
};

struct BatchConfig {
    usize max_items { usize(32) };

    auto validate() const noexcept -> Result<empty, BenchConfigError> {
        if (max_items == usize()) return Err(BenchConfigError::InvalidBatch());
        return Ok(empty {});
    }
};

struct EpochMeasurement {
    ElapsedTime       elapsed;
    u64               iterations;
    CounterSet        counters;
    Vec<MetricSample> metrics;
};

struct WindowMeasurement {
    CounterSet        counters;
    Vec<MetricSample> metrics;
};

struct BenchmarkSummary {
    f64           median_ns_per_unit;
    f64           mean_ns_per_unit;
    f64           minimum_ns_per_unit;
    f64           maximum_ns_per_unit;
    f64           median_absolute_percentage_error;
    f64           units_per_second;
    f64           total_elapsed_ns;
    u64           total_iterations;
    u64           total_items;
    u64           total_bytes;
    Option<f64>   instructions_per_unit;
    Option<f64>   cycles_per_unit;
    Option<f64>   instructions_per_cycle;
    Option<f64>   branches_per_unit;
    Option<f64>   branch_miss_ratio;
    Option<f64>   median_items_per_second;
    Option<f64>   median_bytes_per_second;
    Vec<WorkRate> rates;
    auto          rate(ref<str> name) const -> Option<f64> {
        for (const auto& value : rates)
            if (value.name == name) return Some(f64(value.per_second));
        return None();
    }
};

class BenchmarkResult {
    String                name_;
    RunConfig             run_config_;
    BenchConfig           config_;
    TimingDescriptor      timing_;
    Vec<WorkQuantity>     work_;
    CounterAvailability   counter_availability_;
    Vec<EpochMeasurement> measurements_;
    MeasurementScope      scope_;
    Option<usize>         batch_size_;
    String                collector_identity_;

public:
    BenchmarkResult(String                name,
                    RunConfig             run_config,
                    BenchConfig           config,
                    TimingDescriptor      timing,
                    CounterAvailability   counter_availability,
                    Vec<EpochMeasurement> measurements,
                    MeasurementScope      scope              = MeasurementScope::Repeated,
                    Option<usize>         batch_size         = None(),
                    String                collector_identity = {})
        : name_(rstd::move(name)),
          run_config_(rstd::move(run_config)),
          config_(rstd::move(config)),
          timing_(rstd::move(timing)),
          counter_availability_(rstd::move(counter_availability)),
          measurements_(rstd::move(measurements)),
          scope_(scope),
          batch_size_(batch_size),
          collector_identity_(rstd::move(collector_identity)) {
        if (run_config_.items_per_iteration != u64())
            work_.push({ "items"_Str, WorkUnit::Count, run_config_.items_per_iteration });
        if (run_config_.bytes_per_iteration != u64())
            work_.push({ "bytes"_Str, WorkUnit::Bytes, run_config_.bytes_per_iteration });
        for (const auto& quantity : run_config_.quantities) work_.push(quantity.clone());
    }
    BenchmarkResult(String                name,
                    RunConfig             work,
                    BenchConfig           config,
                    time::Duration        resolution,
                    CounterAvailability   counters,
                    Vec<EpochMeasurement> epochs,
                    MeasurementScope      scope      = MeasurementScope::Repeated,
                    Option<usize>         batch_size = None(),
                    String                collector  = {})
        : BenchmarkResult(rstd::move(name),
                          rstd::move(work),
                          rstd::move(config),
                          TimingDescriptor { "host_monotonic"_Str,
                                             "operation_window"_Str,
                                             Some(f64(resolution.as_secs_f64() * 1e9)) },
                          rstd::move(counters),
                          rstd::move(epochs),
                          scope,
                          batch_size,
                          rstd::move(collector)) {}

    auto name() const noexcept -> ref<str> { return name_.as_str(); }
    auto collector_identity() const noexcept -> ref<str> { return collector_identity_.as_str(); }
    auto unit() const noexcept -> ref<str> { return run_config_.unit.as_str(); }
    auto batch() const noexcept -> f64 { return run_config_.batch; }
    auto scope() const noexcept -> MeasurementScope { return scope_; }
    auto batch_size() const noexcept -> Option<usize> { return batch_size_; }
    auto config() const noexcept -> const BenchConfig& { return config_; }
    auto run_config() const noexcept -> const RunConfig& { return run_config_; }
    auto timing() const noexcept -> const TimingDescriptor& { return timing_; }
    auto work() const noexcept -> slice<WorkQuantity> { return work_.as_slice(); }
    auto validate() const -> Result<empty, String>;
    auto counter_availability() const noexcept -> const CounterAvailability& {
        return counter_availability_;
    }
    auto measurements() const noexcept -> slice<EpochMeasurement> {
        return measurements_.as_slice();
    }
    auto summary() const -> BenchmarkSummary;
    auto same_measurement_config(const BenchmarkResult& other) const -> bool;
};

} // namespace rstd::bench
