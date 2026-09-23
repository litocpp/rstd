export module rstd.bench:benchmark;
export import :statistics;
import :counters;

using namespace rstd::prelude;

template<typename T>
struct BenchSlot {
    T value;
    template<typename Make>
    explicit BenchSlot(Make&& make): value(make()) {}
};

template<typename T>
constexpr bool bench_borrowed_output = false;
template<typename T>
constexpr bool bench_borrowed_output<rstd::ref<T>> = true;
template<typename T>
constexpr bool bench_borrowed_output<rstd::mut_ref<T>> = true;

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

    template<typename Driver>
    [[gnu::noinline]]
    auto run_driver(ref<str>         name,
                    Driver&&         driver,
                    RunConfig        run_config,
                    MeasurementScope scope,
                    Option<usize>    batch_size) -> Result<BenchmarkResult, BenchError> {
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
            auto warmed = driver(config_.warmup_iterations, false);
            if (warmed.is_err()) return Err(rstd::move(warmed).unwrap_err_unchecked());
        }

        auto measurements = Vec<EpochMeasurement>::with_capacity(config_.epochs);
        auto iterations = config_.exact_epoch_iterations.is_some() ? *config_.exact_epoch_iterations
                                                                   : config_.min_epoch_iterations;
        auto const target = target_epoch_ns(resolution);
        usize      zero_elapsed_count;
        u64        adjustment_elapsed;
        u64        adjustment_iterations;

        while (measurements.len() < config_.epochs) {
            auto measured = driver(iterations, true);
            if (measured.is_err()) return Err(rstd::move(measured).unwrap_err_unchecked());
            auto epoch      = rstd::move(measured).unwrap_unchecked();
            auto elapsed    = epoch.elapsed.as_nanos().to_primitive();
            auto elapsed_ns = u64(elapsed);
            if (elapsed_ns == u64()) {
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

            adjustment_elapsed       = bench_saturating_add(adjustment_elapsed, elapsed_ns);
            auto adjusted_iterations = adjustment_iterations.checked_add(iterations);
            if (adjusted_iterations.is_none()) return Err(BenchError::IterationOverflow());
            adjustment_iterations = rstd::move(adjusted_iterations).unwrap_unchecked();

            bool const exact = config_.exact_epoch_iterations.is_some();
            bool const ready = exact || u128(elapsed_ns.to_primitive()) * u128(3) >=
                                            u128(target.to_primitive()) * u128(2);
            if (ready) {
                measurements.push(rstd::move(epoch));
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
                                  rstd::move(measurements),
                                  scope,
                                  batch_size));
    }
    template<typename Op>
    auto measure(u64 iterations, bool timed, Op&& op) -> Result<EpochMeasurement, BenchError> {
        if (timed) counters_.begin_measure();
        auto started   = timed ? clock().now_ns() : u64();
        auto remaining = iterations.to_primitive();
        while (remaining-- > 0) op();
        auto ended    = timed ? clock().now_ns() : u64();
        auto counters = CounterSet {};
        if (timed) {
            auto result = counters_.end_measure(iterations);
            if (result.is_err() && config_.counter_mode.is_Required()) {
                return Err(
                    BenchError::CounterUnavailable(rstd::move(result).unwrap_err_unchecked()));
            }
            if (result.is_ok()) counters = rstd::move(result).unwrap_unchecked();
        }
        if (ended < started) return Err(BenchError::Clock(ClockError::Stalled()));
        return Ok(EpochMeasurement {
            .elapsed    = time::Duration::from_nanos(ended - started),
            .iterations = iterations,
            .counters   = rstd::move(counters),
        });
    }

    template<bool Borrowed, typename Setup, typename Op>
    auto run_batches(ref<str>    name,
                     Setup&&     setup,
                     Op&&        op,
                     BatchConfig batch_config,
                     RunConfig   run_config) -> Result<BenchmarkResult, BenchError> {
        using Input = decltype(setup());
        static_assert(! mtp::is_void<Input> && ! mtp::is_ref<Input> &&
                          ! bench_borrowed_output<mtp::rm_cvf<Input>>,
                      "benchmark setup must return an owned value");
        using Argument = mtp::cond<Borrowed, Input&, Input&&>;
        using Output   = decltype(op(mtp::declval<Argument>()));
        static_assert(! mtp::is_ref<Output> && ! bench_borrowed_output<mtp::rm_cvf<Output>>,
                      "batched benchmarks require owned output or void");
        using StoredOutput = mtp::void_empty_t<Output>;
        auto valid         = batch_config.validate();
        if (valid.is_err())
            return Err(BenchError::InvalidConfig(rstd::move(valid).unwrap_err_unchecked()));
        if (batch_config.max_items > usize::MAX / usize(sizeof(BenchSlot<Input>)) ||
            (! mtp::is_void<Output> &&
             batch_config.max_items > usize::MAX / usize(sizeof(BenchSlot<StoredOutput>)))) {
            return Err(BenchError::InvalidConfig(BenchConfigError::InvalidBatch()));
        }
        auto driver = [&](u64 iterations, bool timed) -> Result<EpochMeasurement, BenchError> {
            auto             remaining = iterations;
            EpochMeasurement total { .iterations = iterations };
            bool             first = true;
            while (remaining != u64()) {
                auto count   = usize((remaining < u64(batch_config.max_items.to_primitive())
                                          ? remaining
                                          : u64(batch_config.max_items.to_primitive()))
                                         .to_primitive());
                auto inputs  = Vec<BenchSlot<Input>>::with_capacity(count);
                auto outputs = Vec<BenchSlot<StoredOutput>>::with_capacity(
                    mtp::is_void<Output> ? usize() : count);
                for (usize index; index < count; ++index) inputs.emplace_back(setup);
                rstd::hint::black_box(inputs.as_mut_slice());
                usize index;
                auto  measured = measure(u64(count.to_primitive()), timed, [&] {
                    auto invoke = [&]() -> decltype(auto) {
                        if constexpr (Borrowed)
                            return op(inputs[index].value);
                        else
                            return op(rstd::move(inputs[index].value));
                    };
                    if constexpr (mtp::is_void<Output>)
                        invoke();
                    else
                        outputs.emplace_back(invoke);
                    ++index;
                });
                rstd::hint::black_box(inputs.as_mut_slice());
                rstd::hint::black_box(outputs.as_mut_slice());
                if (measured.is_err()) return Err(rstd::move(measured).unwrap_err_unchecked());
                auto part    = rstd::move(measured).unwrap_unchecked();
                auto elapsed = u64(total.elapsed.as_nanos().to_primitive())
                                   .checked_add(u64(part.elapsed.as_nanos().to_primitive()));
                if (elapsed.is_none()) return Err(BenchError::IterationOverflow());
                total.elapsed = time::Duration::from_nanos(*elapsed);
                if (first)
                    total.counters = rstd::move(part.counters);
                else
                    merge_counters(total.counters, part.counters);
                first = false;
                remaining -= u64(count.to_primitive());
            }
            return Ok(rstd::move(total));
        };
        return run_driver(name,
                          driver,
                          rstd::move(run_config),
                          Borrowed ? MeasurementScope::BatchedRef : MeasurementScope::Batched,
                          Some(batch_config.max_items));
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

    /// Returned values are consumed and destroyed inside the measured loop.
    template<typename Op>
    auto run(ref<str> name, Op&& op, RunConfig run_config = {})
        -> Result<BenchmarkResult, BenchError> {
        auto driver = [&](u64 iterations, bool timed) {
            return measure(iterations, timed, [&] {
                if constexpr (mtp::is_void<decltype(op())>)
                    op();
                else
                    rstd::hint::black_box(op());
            });
        };
        return run_driver(name, driver, rstd::move(run_config), MeasurementScope::Repeated, None());
    }

    /// Setup and output destruction are excluded; destruction inside op is measured.
    template<typename Setup, typename Op>
    auto run_batched(ref<str>    name,
                     Setup&&     setup,
                     Op&&        op,
                     BatchConfig batch_config = {},
                     RunConfig   run_config   = {}) -> Result<BenchmarkResult, BenchError> {
        return run_batches<false>(name,
                                  rstd::forward<Setup>(setup),
                                  rstd::forward<Op>(op),
                                  batch_config,
                                  rstd::move(run_config));
    }

    /// Inputs stay owned by the runner; outputs must not borrow from them.
    template<typename Setup, typename Op>
    auto run_batched_ref(ref<str>    name,
                         Setup&&     setup,
                         Op&&        op,
                         BatchConfig batch_config = {},
                         RunConfig   run_config   = {}) -> Result<BenchmarkResult, BenchError> {
        return run_batches<true>(name,
                                 rstd::forward<Setup>(setup),
                                 rstd::forward<Op>(op),
                                 batch_config,
                                 rstd::move(run_config));
    }
};

export using Bench = BasicBench<SteadyClock>;

} // namespace rstd::bench
