export module rstd.bench:external;
export import :statistics;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace rstd::bench
{
struct ExternalSamplingConfig {
    usize epochs { 11 };
    u64   iterations { 1 };
    auto  validate() const -> Result<empty, BenchError> {
        if (epochs == usize())
            return Err(BenchError::InvalidConfig(BenchConfigError::ZeroEpochs()));
        if (iterations == u64())
            return Err(BenchError::InvalidConfig(BenchConfigError::ZeroMinIterations()));
        if (iterations.checked_mul(u64(epochs.to_primitive())).is_none())
            return Err(BenchError::IterationOverflow());
        return Ok(empty {});
    }
};
class ExternalBench {
public:
    template<class Measure>
    auto run(ref<str>               name,
             Measure&&              measure,
             ExternalSamplingConfig sampling,
             TimingDescriptor       timing,
             RunConfig              work = {}) -> Result<BenchmarkResult, BenchError> {
        if (name.is_empty()) return Err(BenchError::Operation("empty measurement name"_Str));
        auto config_valid = sampling.validate();
        if (config_valid.is_err()) return Err(rstd::move(config_valid).unwrap_err());
        auto work_valid = work.validate();
        if (work_valid.is_err())
            return Err(BenchError::InvalidConfig(rstd::move(work_valid).unwrap_err()));
        auto timing_valid = timing.validate();
        if (timing_valid.is_err())
            return Err(BenchError::Operation(rstd::move(timing_valid).unwrap_err()));
        BenchConfig config;
        config.epochs                 = sampling.epochs;
        config.exact_epoch_iterations = Some(u64(sampling.iterations));
        Vec<EpochMeasurement> epochs;
        for (usize i; i < sampling.epochs; ++i) {
            auto result = measure(sampling.iterations);
            if (result.is_err()) return Err(rstd::move(result).unwrap_err());
            auto epoch = rstd::move(result).unwrap();
            if (! epoch.elapsed.valid() || epoch.iterations != sampling.iterations)
                return Err(BenchError::Operation("invalid external epoch"_Str));
            epochs.push(rstd::move(epoch));
        }
        auto result = BenchmarkResult(String::make(name),
                                      rstd::move(work),
                                      config,
                                      rstd::move(timing),
                                      CounterAvailability::Disabled(),
                                      rstd::move(epochs),
                                      MeasurementScope::External,
                                      None(),
                                      "none/v1"_Str);
        auto valid  = result.validate();
        if (valid.is_err()) return Err(BenchError::Operation(rstd::move(valid).unwrap_err()));
        return Ok(rstd::move(result));
    }
};
} // namespace rstd::bench
