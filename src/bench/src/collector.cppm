export module rstd.bench:collector;
export import :model;
import :counters;

using namespace rstd::prelude;

export namespace rstd::bench
{

template<typename T>
concept MeasurementCollector = requires(T& collector, const T& constant, u64 iterations) {
    { collector.begin() } -> mtp::same_as<Result<empty, BenchError>>;
    { collector.end(iterations) } -> mtp::same_as<Result<WindowMeasurement, BenchError>>;
    { collector.abort() } -> mtp::same_as<Result<empty, BenchError>>;
    { collector.availability() } -> mtp::same_as<CounterAvailability>;
    { constant.identity() } -> mtp::same_as<String>;
};

struct NoopCollector {
    auto identity() const -> String { return String::make("none/v1"_str); }
    auto begin() -> Result<empty, BenchError> { return Ok(empty {}); }
    auto end(u64) -> Result<WindowMeasurement, BenchError> { return Ok(WindowMeasurement {}); }
    auto abort() -> Result<empty, BenchError> { return Ok(empty {}); }
    auto availability() const -> CounterAvailability { return CounterAvailability::Disabled(); }
};

auto join_collector_errors(BenchError primary, BenchError cleanup) -> BenchError;
auto join_windows(WindowMeasurement first, WindowMeasurement second)
    -> Result<WindowMeasurement, BenchError>;

template<MeasurementCollector First, MeasurementCollector Second>
class CollectorPair {
    First  first_;
    Second second_;

public:
    auto identity() const -> String {
        return rstd::format("pair/v1({},{})", first_.identity(), second_.identity());
    }
    CollectorPair(First first, Second second)
        : first_(rstd::move(first)), second_(rstd::move(second)) {}

    auto availability() const -> CounterAvailability {
        auto first  = first_.availability();
        auto second = second_.availability();
        if (first.is_Unavailable()) return first;
        if (second.is_Unavailable()) return second;
        if (first.is_Disabled()) return second;
        if (second.is_Disabled()) return first;
        return CounterAvailability::Available(first.as_Available().mask |
                                              second.as_Available().mask);
    }

    template<typename Probe>
    auto calibrate(Probe probe) -> Result<empty, BenchError> {
        if constexpr (requires { first_.calibrate(probe); }) {
            auto result = first_.calibrate(probe);
            if (result.is_err()) return result;
        }
        if constexpr (requires { second_.calibrate(probe); }) return second_.calibrate(probe);
        return Ok(empty {});
    }

    auto begin() -> Result<empty, BenchError> {
        auto first = first_.begin();
        if (first.is_err()) return first;
        auto second = second_.begin();
        if (second.is_err()) {
            auto cleanup = first_.abort();
            if (cleanup.is_err())
                return Err(join_collector_errors(rstd::move(second).unwrap_err(),
                                                 rstd::move(cleanup).unwrap_err()));
        }
        return second;
    }

    auto end(u64 iterations) -> Result<WindowMeasurement, BenchError> {
        auto second = second_.end(iterations);
        auto first  = first_.end(iterations);
        if (second.is_err() && first.is_err())
            return Err(join_collector_errors(rstd::move(second).unwrap_err(),
                                             rstd::move(first).unwrap_err()));
        if (second.is_err()) return second;
        if (first.is_err()) return first;
        return join_windows(rstd::move(first).unwrap(), rstd::move(second).unwrap());
    }

    auto abort() -> Result<empty, BenchError> {
        auto second = second_.abort();
        auto first  = first_.abort();
        if (second.is_err() && first.is_err())
            return Err(join_collector_errors(rstd::move(second).unwrap_err(),
                                             rstd::move(first).unwrap_err()));
        if (second.is_err()) return second;
        return first;
    }
};

template<MeasurementCollector First, MeasurementCollector Second>
auto collectors(First first, Second second) -> CollectorPair<First, Second> {
    return { rstd::move(first), rstd::move(second) };
}

template<MeasurementCollector First, MeasurementCollector Second, MeasurementCollector... Rest>
    requires(sizeof...(Rest) > 0)
auto collectors(First first, Second second, Rest... rest) {
    return collectors(rstd::move(first), collectors(rstd::move(second), rstd::move(rest)...));
}

class PmuCollector {
    CounterBackend backend_;
    bool           required_;
    bool           disabled_;

public:
    explicit PmuCollector(CounterMode mode = CounterMode::Disabled())
        : backend_(mode), required_(mode.is_Required()), disabled_(mode.is_Disabled()) {}
    auto identity() const -> String {
        return String::make(disabled_   ? "pmu/v1/disabled"_str
                            : required_ ? "pmu/v1/required"_str
                                        : "pmu/v1/auto"_str);
    }
    auto availability() const -> CounterAvailability { return backend_.availability(); }

    template<typename Probe>
    auto calibrate(Probe probe) -> Result<empty, BenchError> {
        backend_.calibrate_measurement(rstd::move(probe));
        auto state = availability();
        if (required_ && state.is_Unavailable())
            return Err(BenchError::CounterUnavailable(state.as_Unavailable().code));
        return Ok(empty {});
    }

    auto begin() -> Result<empty, BenchError> {
        backend_.begin_measure();
        auto state = availability();
        if (required_ && state.is_Unavailable())
            return Err(BenchError::CounterUnavailable(state.as_Unavailable().code));
        return Ok(empty {});
    }

    auto end(u64 iterations) -> Result<WindowMeasurement, BenchError> {
        auto measured = backend_.end_measure(iterations);
        if (measured.is_err()) {
            if (required_)
                return Err(BenchError::CounterUnavailable(rstd::move(measured).unwrap_err()));
            return Ok(WindowMeasurement {});
        }
        return Ok(WindowMeasurement { rstd::move(measured).unwrap(), {} });
    }

    auto abort() -> Result<empty, BenchError> {
        auto result = end(u64());
        if (result.is_err()) return Err(rstd::move(result).unwrap_err());
        return Ok(empty {});
    }
};

} // namespace rstd::bench
