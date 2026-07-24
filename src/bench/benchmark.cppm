export module rstd.bench:benchmark;
export import :statistics;
import :counters;

using namespace rstd::prelude;

namespace rstd::bench
{

export template<typename Clock>
    requires Impled<Clock, MonotonicClock>
class BasicBench {
    Clock                  clock_;
    BenchConfig            config_;
    CounterBackend         counters_;
    Option<time::Duration> resolution_;
    u64                    jitter_state_;

    decltype(auto) clock() const noexcept { return as<MonotonicClock>(clock_); }

    auto next_jitter() noexcept -> f64 {
        auto value = jitter_state_.to_primitive();
        value ^= value << 13u;
        value ^= value >> 7u;
        value ^= value << 17u;
        jitter_state_ = u64(value);
        auto fraction = static_cast<double>(value & 0xffffu) / 65535.0;
        return f64(1.0 + fraction * 0.2);
    }

    auto target_epoch_ns(time::Duration resolution) const noexcept -> u64 {
        auto target =
            resolution.as_nanos() * u128(config_.clock_resolution_multiple.to_primitive());
        auto minimum = config_.min_epoch_time.as_nanos();
        auto maximum = config_.max_epoch_time.as_nanos();
        if (target < minimum) target = minimum;
        if (target > maximum) target = maximum;
        if (target > u128(u64::MAX.to_primitive())) return u64::MAX;
        return u64(target.to_primitive());
    }

    auto estimate_iterations(u64 elapsed, u64 iterations, u64 target) -> Result<u64, BenchError> {
        if (elapsed == u64()) return Err(BenchError::OperationOptimizedAway());
        if (elapsed < target / u64(10)) {
            auto increased = iterations.checked_mul(u64(10));
            if (increased.is_none()) return Err(BenchError::IterationOverflow());
            return Ok(rstd::move(increased).unwrap_unchecked());
        }

        auto estimate = static_cast<long double>(target.to_primitive()) /
                        static_cast<long double>(elapsed.to_primitive()) *
                        static_cast<long double>(iterations.to_primitive()) *
                        static_cast<long double>(next_jitter().to_primitive());
        auto minimum  = static_cast<long double>(config_.min_epoch_iterations.to_primitive());
        if (estimate < minimum) estimate = minimum;
        if (estimate > static_cast<long double>(u64::MAX.to_primitive())) {
            return Err(BenchError::IterationOverflow());
        }
        return Ok(u64(static_cast<rstd::uint64_t>(estimate + 0.5L)));
    }

public:
    explicit BasicBench(Clock clock, BenchConfig config = {})
        : clock_(rstd::move(clock)),
          config_(rstd::move(config)),
          counters_(config_.counter_mode),
          jitter_state_(config_.jitter_seed == u64() ? u64(123) : config_.jitter_seed) {}

    static auto new_(BenchConfig config = {}) -> BasicBench
        requires requires { Clock {}; }
    {
        return BasicBench(Clock {}, rstd::move(config));
    }

    auto config() const noexcept -> const BenchConfig& { return config_; }

    template<typename Op>
    [[gnu::noinline]]
    auto run(ref<str> name, Op&& op, RunConfig run_config = {})
        -> Result<BenchmarkResult, BenchError> {
        auto config_validation = config_.validate();
        if (config_validation.is_err()) {
            return Err(
                BenchError::InvalidConfig(rstd::move(config_validation).unwrap_err_unchecked()));
        }
        auto run_validation = run_config.validate();
        if (run_validation.is_err()) {
            return Err(
                BenchError::InvalidConfig(rstd::move(run_validation).unwrap_err_unchecked()));
        }

        if (resolution_.is_none()) {
            auto resolution = clock().resolution();
            if (resolution.is_err()) {
                return Err(BenchError::Clock(rstd::move(resolution).unwrap_err_unchecked()));
            }
            resolution_ = Some(rstd::move(resolution).unwrap_unchecked());
        }
        counters_.calibrate_measurement([this] {
            rstd::hint::black_box(clock().now_ns());
            rstd::hint::black_box(clock().now_ns());
        });
        auto resolution   = *resolution_;
        auto availability = counters_.availability();
        if (config_.counter_mode.is_Required() && availability.is_Unavailable()) {
            return Err(BenchError::CounterUnavailable(availability.as_Unavailable().code));
        }

        if (config_.warmup_iterations != u64()) {
            auto remaining = config_.warmup_iterations.to_primitive();
            while (remaining-- > 0) op();
        }

        auto measurements = Vec<EpochMeasurement>::with_capacity(config_.epochs);
        auto iterations = config_.exact_epoch_iterations.is_some() ? *config_.exact_epoch_iterations
                                                                   : config_.min_epoch_iterations;
        auto const target = target_epoch_ns(resolution);
        usize      zero_elapsed_count;
        u64        adjustment_elapsed;
        u64        adjustment_iterations;

        while (measurements.len() < config_.epochs) {
            counters_.begin_measure();
            auto const started   = clock().now_ns();
            auto       remaining = iterations.to_primitive();
            while (remaining-- > 0) op();
            auto const ended          = clock().now_ns();
            auto       counter_result = counters_.end_measure(iterations);
            if (counter_result.is_err() && config_.counter_mode.is_Required()) {
                return Err(BenchError::CounterUnavailable(
                    rstd::move(counter_result).unwrap_err_unchecked()));
            }
            auto counters = counter_result.is_ok() ? rstd::move(counter_result).unwrap_unchecked()
                                                   : CounterSet {};

            if (ended < started) return Err(BenchError::Clock(ClockError::Stalled()));
            auto elapsed = ended - started;
            if (elapsed == u64()) {
                ++zero_elapsed_count;
                if (zero_elapsed_count >= usize(3)) {
                    return Err(BenchError::OperationOptimizedAway());
                }
                auto increased = iterations.checked_mul(u64(10));
                if (increased.is_none()) return Err(BenchError::IterationOverflow());
                iterations = rstd::move(increased).unwrap_unchecked();
                continue;
            } else {
                zero_elapsed_count = usize();
            }

            adjustment_elapsed       = bench_saturating_add(adjustment_elapsed, elapsed);
            auto adjusted_iterations = adjustment_iterations.checked_add(iterations);
            if (adjusted_iterations.is_none()) return Err(BenchError::IterationOverflow());
            adjustment_iterations = rstd::move(adjusted_iterations).unwrap_unchecked();

            bool const exact = config_.exact_epoch_iterations.is_some();
            bool const ready = exact || u128(elapsed.to_primitive()) * u128(3) >=
                                            u128(target.to_primitive()) * u128(2);
            if (ready) {
                measurements.push(EpochMeasurement {
                    .elapsed    = time::Duration::from_nanos(elapsed),
                    .iterations = iterations,
                    .counters   = rstd::move(counters),
                });
            }
            if (! exact) {
                auto estimate =
                    estimate_iterations(adjustment_elapsed, adjustment_iterations, target);
                if (estimate.is_err()) return Err(rstd::move(estimate).unwrap_err_unchecked());
                iterations = rstd::move(estimate).unwrap_unchecked();
            }
        }

        return Ok(BenchmarkResult(String::make(name),
                                  rstd::move(run_config),
                                  config_,
                                  resolution,
                                  counters_.availability(),
                                  rstd::move(measurements)));
    }
};

export using Bench = BasicBench<SteadyClock>;

} // namespace rstd::bench
