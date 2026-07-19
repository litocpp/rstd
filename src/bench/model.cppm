module;
#include <rstd/enum.hpp>

export module rstd.bench:model;
export import :clock;

using namespace rstd::prelude;

#define RSTD_BENCH_COUNTER_MODE_VARIANTS(V) \
    V(Disabled)                             \
    V(Auto)                                 \
    V(Required)

#define RSTD_BENCH_COUNTER_AVAILABILITY_VARIANTS(V) \
    V(Disabled, ())                                 \
    V(Available, (u32 mask;))                       \
    V(Unavailable, (i32 code;))

#define RSTD_BENCH_CONFIG_ERROR_VARIANTS(V) \
    V(ZeroEpochs)                           \
    V(ZeroMinIterations)                    \
    V(ZeroClockResolutionMultiple)          \
    V(ZeroMaxEpochTime)                     \
    V(InvalidEpochRange)                    \
    V(InvalidBatch)

#define RSTD_BENCH_ERROR_VARIANTS(V)             \
    V(InvalidConfig, (BenchConfigError reason;)) \
    V(Clock, (ClockError reason;))               \
    V(IterationOverflow, ())                     \
    V(OperationOptimizedAway, ())                \
    V(CounterUnavailable, (i32 code;))

export namespace rstd::bench
{

RSTD_TAG_ENUM(CounterMode, RSTD_BENCH_COUNTER_MODE_VARIANTS)
RSTD_ENUM(CounterAvailability, RSTD_BENCH_COUNTER_AVAILABILITY_VARIANTS)
RSTD_TAG_ENUM(BenchConfigError, RSTD_BENCH_CONFIG_ERROR_VARIANTS)
RSTD_ENUM(BenchError, RSTD_BENCH_ERROR_VARIANTS)

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
    CounterMode    counter_mode { CounterMode::Auto() };
    u64            jitter_seed { u64(123) };

    auto validate() const noexcept -> Result<empty, BenchConfigError> {
        if (epochs == usize()) return Err(BenchConfigError::ZeroEpochs());
        if (min_epoch_iterations == u64()) return Err(BenchConfigError::ZeroMinIterations());
        if (clock_resolution_multiple == usize()) {
            return Err(BenchConfigError::ZeroClockResolutionMultiple());
        }
        if (max_epoch_time.is_zero()) return Err(BenchConfigError::ZeroMaxEpochTime());
        if (min_epoch_time > max_epoch_time) return Err(BenchConfigError::InvalidEpochRange());
        if (exact_epoch_iterations.is_some() && *exact_epoch_iterations == u64()) {
            return Err(BenchConfigError::ZeroMinIterations());
        }
        return Ok(empty {});
    }
};

struct RunConfig {
    String unit { String::make("op") };
    f64    batch { f64(1.0) };
    u64    items_per_iteration {};
    u64    bytes_per_iteration {};

    auto validate() const noexcept -> Result<empty, BenchConfigError> {
        if (! batch.is_finite() || batch <= f64()) return Err(BenchConfigError::InvalidBatch());
        return Ok(empty {});
    }
};

struct EpochMeasurement {
    time::Duration elapsed;
    u64            iterations;
    CounterSet     counters;
};

struct BenchmarkSummary {
    f64            median_ns_per_unit;
    f64            mean_ns_per_unit;
    f64            minimum_ns_per_unit;
    f64            maximum_ns_per_unit;
    f64            median_absolute_percentage_error;
    f64            units_per_second;
    time::Duration total_elapsed;
    u64            total_iterations;
    u64            total_items;
    u64            total_bytes;
    Option<f64>    instructions_per_unit;
    Option<f64>    cycles_per_unit;
    Option<f64>    instructions_per_cycle;
    Option<f64>    branches_per_unit;
    Option<f64>    branch_miss_ratio;
};

class BenchmarkResult {
    String                name_;
    RunConfig             run_config_;
    BenchConfig           config_;
    time::Duration        clock_resolution_;
    CounterAvailability   counter_availability_;
    Vec<EpochMeasurement> measurements_;

public:
    BenchmarkResult(String                name,
                    RunConfig             run_config,
                    BenchConfig           config,
                    time::Duration        clock_resolution,
                    CounterAvailability   counter_availability,
                    Vec<EpochMeasurement> measurements)
        : name_(rstd::move(name)),
          run_config_(rstd::move(run_config)),
          config_(rstd::move(config)),
          clock_resolution_(clock_resolution),
          counter_availability_(rstd::move(counter_availability)),
          measurements_(rstd::move(measurements)) {}

    auto name() const noexcept -> ref<str> { return name_.as_str(); }
    auto unit() const noexcept -> ref<str> { return run_config_.unit.as_str(); }
    auto batch() const noexcept -> f64 { return run_config_.batch; }
    auto config() const noexcept -> const BenchConfig& { return config_; }
    auto run_config() const noexcept -> const RunConfig& { return run_config_; }
    auto clock_resolution() const noexcept -> time::Duration { return clock_resolution_; }
    auto counter_availability() const noexcept -> const CounterAvailability& {
        return counter_availability_;
    }
    auto measurements() const noexcept -> slice<EpochMeasurement> {
        return measurements_.as_slice();
    }
    auto summary() const -> BenchmarkSummary;
};

} // namespace rstd::bench

#undef RSTD_BENCH_COUNTER_MODE_VARIANTS
#undef RSTD_BENCH_COUNTER_AVAILABILITY_VARIANTS
#undef RSTD_BENCH_CONFIG_ERROR_VARIANTS
#undef RSTD_BENCH_ERROR_VARIANTS
