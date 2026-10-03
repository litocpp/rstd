module rstd.bench;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace rstd::bench;

auto ParameterValue::clone() const -> ParameterValue {
    if (is_Unsigned()) return Unsigned(as_Unsigned().value);
    if (is_Signed()) return Signed(as_Signed().value);
    if (is_Real()) return Real(as_Real().value);
    if (is_Boolean()) return Boolean(as_Boolean().value);
    return Text(as_Text().value.clone());
}

auto CaseDescriptor::clone() const -> CaseDescriptor {
    CaseDescriptor result { id.clone(), suite.clone(), revision.clone(), implementation.clone() };
    result.quick_iterations = quick_iterations;
    result.name             = name.clone();
    for (const auto& parameter : parameters)
        result.parameters.push(Parameter { parameter.name.clone(), parameter.value.clone() });
    if (dataset.is_some()) {
        const auto& value = *dataset;
        result.dataset    = Some(DatasetDescriptor { value.id.clone(),
                                                     value.revision.clone(),
                                                     value.digest_algorithm.clone(),
                                                     value.digest.clone(),
                                                     value.input_bytes,
                                                     value.seed });
    }
    return result;
}

auto CaseDescriptor::validate() const -> Result<empty, String> {
    if (id.is_empty() || suite.is_empty() || revision.is_empty())
        return Err("benchmark id, suite and revision must be nonempty"_Str);
    if (quick_iterations == u64()) return Err("quick iterations must be nonzero"_Str);
    if (dataset.is_some() && (dataset->id.is_empty() || dataset->revision.is_empty() ||
                              dataset->digest_algorithm.is_empty() || dataset->digest.is_empty()))
        return Err("dataset requires identity, revision and content digest"_Str);
    for (usize i; i < parameters.len(); ++i) {
        const auto& parameter = parameters[i];
        if (parameter.name.is_empty()) return Err("parameter name must be nonempty"_Str);
        if (parameter.value.is_Real() && ! parameter.value.as_Real().value.is_finite())
            return Err("parameter must be finite"_Str);
        for (usize j; j < i; ++j)
            if (parameters[j].name == parameter.name) return Err("duplicate parameter"_Str);
    }
    return Ok(empty {});
}

auto rstd::bench::describe_error(const BenchError& error) -> String {
    if (error.is_Collector()) return error.as_Collector().reason.clone();
    if (error.is_Operation()) return error.as_Operation().reason.clone();
    if (error.is_CounterUnavailable())
        return rstd::format("performance counter unavailable: {}",
                            error.as_CounterUnavailable().code);
    if (error.is_IterationOverflow()) return "iteration or elapsed-time overflow"_Str;
    if (error.is_OperationOptimizedAway()) return "clock did not resolve operation duration"_Str;
    if (error.is_Clock()) return "benchmark clock failed"_Str;
    const auto& reason = error.as_InvalidConfig().reason;
    if (reason.is_ZeroEpochs()) return "epochs must be nonzero"_Str;
    if (reason.is_ZeroMinIterations()) return "iterations must be nonzero"_Str;
    if (reason.is_ZeroClockResolutionMultiple())
        return "clock resolution multiple must be nonzero"_Str;
    if (reason.is_ZeroMaxEpochTime()) return "maximum epoch duration must be nonzero"_Str;
    if (reason.is_InvalidEpochRange()) return "invalid epoch duration range"_Str;
    return "invalid batch configuration"_Str;
}

auto rstd::bench::merge_metrics(Vec<MetricSample>& total, Vec<MetricSample> part)
    -> Result<empty, BenchError> {
    for (auto& sample : part) {
        bool merged = false;
        for (auto& existing : total) {
            if (existing.name != sample.name) continue;
            if (existing.unit != sample.unit || existing.aggregation != sample.aggregation ||
                existing.direction != sample.direction)
                return Err(BenchError::Collector("metric schema changed between windows"_Str));
            if (existing.aggregation == MetricAggregation::Sum) {
                auto sum = existing.value.checked_add(sample.value);
                if (sum.is_none()) return Err(BenchError::Collector("metric sum overflow"_Str));
                existing.value = *sum;
            } else if (existing.aggregation == MetricAggregation::Maximum) {
                if (sample.value > existing.value) existing.value = sample.value;
            } else
                existing.value = sample.value;
            merged = true;
            break;
        }
        if (! merged) total.push(rstd::move(sample));
    }
    return Ok(empty {});
}

auto rstd::bench::join_collector_errors(BenchError primary, BenchError cleanup) -> BenchError {
    return BenchError::Collector(
        rstd::format("{}; cleanup: {}", describe_error(primary), describe_error(cleanup)));
}

auto rstd::bench::join_windows(WindowMeasurement first, WindowMeasurement second)
    -> Result<WindowMeasurement, BenchError> {
    auto combine = [](Option<u64>& a, Option<u64> b) -> bool {
        if (a.is_some() && b.is_some()) return false;
        if (b.is_some()) a = b;
        return true;
    };
    if (! combine(first.counters.page_faults, second.counters.page_faults) ||
        ! combine(first.counters.cpu_cycles, second.counters.cpu_cycles) ||
        ! combine(first.counters.context_switches, second.counters.context_switches) ||
        ! combine(first.counters.instructions, second.counters.instructions) ||
        ! combine(first.counters.branch_instructions, second.counters.branch_instructions) ||
        ! combine(first.counters.branch_misses, second.counters.branch_misses))
        return Err(BenchError::Collector("duplicate counter providers"_Str));
    for (auto& sample : second.metrics) {
        for (const auto& existing : first.metrics)
            if (existing.name == sample.name)
                return Err(BenchError::Collector("duplicate metric providers"_Str));
        first.metrics.push(rstd::move(sample));
    }
    return Ok(rstd::move(first));
}

auto rstd::bench::failed(String reason, Result<empty, String> cleanup) -> CaseOutcome {
    auto errors = Vec<String>::make();
    errors.push(rstd::move(reason));
    if (cleanup.is_err()) errors.push(rstd::move(cleanup).unwrap_err());
    return CaseOutcome::Failed(rstd::move(errors));
}

auto rstd::bench::complete_measurement(Result<BenchmarkResult, BenchError> measurement,
                                       Result<empty, String>               validation,
                                       Result<empty, String>               cleanup) -> CaseOutcome {
    auto errors = Vec<String>::make();
    if (measurement.is_err()) errors.push(describe_error(measurement.unwrap_err()));
    if (validation.is_err()) errors.push(rstd::move(validation).unwrap_err());
    if (cleanup.is_err()) errors.push(rstd::move(cleanup).unwrap_err());
    if (! errors.is_empty()) return CaseOutcome::Failed(rstd::move(errors));
    return CaseOutcome::Passed(rstd::move(measurement).unwrap());
}

auto RunnerOptions::validate() const -> Result<empty, String> {
    if (repeats == u64()) return Err("repeats must be nonzero"_Str);
    auto valid = sampling.validate();
    if (valid.is_err())
        return Err(describe_error(BenchError::InvalidConfig(rstd::move(valid).unwrap_err())));
    return Ok(empty {});
}

auto RunnerOptions::matches(const CaseDescriptor& descriptor) const -> bool {
    return (suite.is_none() || *suite == descriptor.suite) &&
           (id.is_none() || *id == descriptor.id) && (name.is_none() || *name == descriptor.name);
}

auto RunnerOptions::config_for(const CaseDescriptor& descriptor) const -> BenchConfig {
    auto config = sampling;
    if (quick) {
        config.epochs            = usize(3);
        config.warmup_iterations = u64(1);
        config.counter_mode      = CounterMode::Disabled();
        if (config.exact_epoch_iterations.is_none())
            config.exact_epoch_iterations = Some(u64(descriptor.quick_iterations));
    }
    return config;
}

auto RunReport::passed() const -> bool {
    for (const auto& result : results)
        if (result.outcome.is_Failed()) return false;
    return true;
}

auto RunReport::validate() const -> Result<empty, String> {
    auto valid_environment = environment.validate();
    if (valid_environment.is_err()) return valid_environment;
    if (mode != RunMode::Timing && mode != RunMode::Diagnostic)
        return Err("invalid report mode"_Str);
    for (usize index; index < results.len(); ++index) {
        const auto& result = results[index];
        auto        valid  = result.validate();
        if (valid.is_err()) return valid;
        if ((mode == RunMode::Timing && result.outcome.is_Diagnosed()) ||
            (mode == RunMode::Diagnostic && result.outcome.is_Passed()))
            return Err("case outcome disagrees with report mode"_Str);
        for (usize previous; previous < index; ++previous) {
            const auto& before = results[previous];
            if (before.descriptor.id != result.descriptor.id) continue;
            if (before.repetition == result.repetition) return Err("duplicate case repetition"_Str);
            if (! before.descriptor.same_workload(result.descriptor) ||
                before.descriptor.implementation != result.descriptor.implementation)
                return Err("case identity changed between repetitions"_Str);
            if (before.outcome.is_Passed() && result.outcome.is_Passed() &&
                ! before.outcome.as_Passed().measurement.same_measurement_config(
                    result.outcome.as_Passed().measurement))
                return Err("measurement configuration changed between repetitions"_Str);
            if (before.outcome.is_Diagnosed() && result.outcome.is_Diagnosed()) {
                const auto& a = before.outcome.as_Diagnosed().measurement;
                const auto& b = result.outcome.as_Diagnosed().measurement;
                if (a.iterations != b.iterations || a.scope != b.scope ||
                    a.batch_size != b.batch_size || a.work.unit != b.work.unit ||
                    a.work.batch != b.work.batch ||
                    a.work.items_per_iteration != b.work.items_per_iteration ||
                    a.work.bytes_per_iteration != b.work.bytes_per_iteration)
                    return Err("diagnostic configuration changed between repetitions"_Str);
            }
        }
    }
    return Ok(empty {});
}

auto RunResult::validate() const -> Result<empty, String> {
    const auto& result = *this;
    auto        valid  = descriptor.validate();
    if (valid.is_err()) return valid;
    if (result.outcome.is_Failed()) {
        if (result.outcome.as_Failed().errors.is_empty())
            return Err("failed case has no errors"_Str);
        for (const auto& error : result.outcome.as_Failed().errors)
            if (error.is_empty()) return Err("empty case error"_Str);
        return Ok(empty {});
    }
    if (result.outcome.is_Skipped()) {
        if (result.outcome.as_Skipped().reason.is_empty())
            return Err("skipped case has no reason"_Str);
        return Ok(empty {});
    }
    if (result.outcome.is_Diagnosed()) return result.outcome.as_Diagnosed().measurement.validate();
    const auto& measured = result.outcome.as_Passed().measurement;
    if (measured.collector_identity().is_empty()) return Err("missing collector identity"_Str);
    if (measured.name() != result.descriptor.id.as_str())
        return Err("measurement name differs from case id"_Str);
    auto config = measured.config().validate();
    if (config.is_err())
        return Err(describe_error(BenchError::InvalidConfig(rstd::move(config).unwrap_err())));
    auto work = measured.run_config().validate();
    if (work.is_err() || measured.run_config().unit.is_empty())
        return Err("invalid work amount"_Str);
    if (measured.measurements().len() != measured.config().epochs)
        return Err("epoch count differs from configuration"_Str);
    if (measured.clock_resolution().is_zero()) return Err("clock resolution must be nonzero"_Str);
    if ((measured.scope() == MeasurementScope::Repeated) != measured.batch_size().is_none() ||
        (measured.batch_size().is_some() && *measured.batch_size() == usize()))
        return Err("scope and batch size disagree"_Str);
    for (const auto& epoch : measured.measurements()) {
        if (epoch.elapsed.is_zero() || epoch.iterations == u64()) return Err("invalid epoch"_Str);
        if (measured.config().exact_epoch_iterations.is_some() &&
            *measured.config().exact_epoch_iterations != epoch.iterations)
            return Err("epoch iterations differ from exact configuration"_Str);
        for (usize i; i < epoch.metrics.len(); ++i) {
            const auto& metric = epoch.metrics[i];
            if (metric.name.is_empty()) return Err("empty metric name"_Str);
            for (usize j; j < i; ++j)
                if (metric.name == epoch.metrics[j].name) return Err("duplicate metric name"_Str);
        }
    }
    return Ok(empty {});
}

auto RunEnvironment::clone() const -> RunEnvironment {
    RunEnvironment result;
    for (const auto& field : fields) {
        Option<String> value;
        if (field.value.is_some()) value = Some(field.value->clone());
        result.fields.push({ field.name.clone(), rstd::move(value), field.source.clone() });
    }
    return result;
}

auto RunEnvironment::validate() const -> Result<empty, String> {
    for (usize i; i < fields.len(); ++i) {
        if (fields[i].name.is_empty() || fields[i].source.is_empty())
            return Err("environment field requires name and source"_Str);
        for (usize j; j < i; ++j)
            if (fields[i].name == fields[j].name) return Err("duplicate environment field"_Str);
    }
    return Ok(empty {});
}

auto allocation_value_text(const DiagnosticValue& value) -> String {
    if (value.is_Count()) return rstd::format("{}", value.as_Count().value);
    if (value.is_Ratio())
        return rstd::format("{}/{}", value.as_Ratio().numerator, value.as_Ratio().denominator);
    if (value.is_Unavailable())
        return rstd::format("unavailable: {}", value.as_Unavailable().reason);
    return "overflow"_Str;
}

auto TextReporter::report(const RunResult& result) -> Result<empty, String> {
    String line;
    if (result.outcome.is_Passed()) {
        const auto& measured = result.outcome.as_Passed().measurement;
        line = rstd::format("{} {} ns/op\n",
                            result.descriptor.id,
                            measured.summary().median_ns_per_unit * measured.run_config().batch);
    } else if (result.outcome.is_Diagnosed()) {
        const auto& measured = result.outcome.as_Diagnosed().measurement;
        line                 = rstd::format(
            "{} diagnostic ({} operations)\n", result.descriptor.id, measured.iterations);
        if (measured.allocations.is_some()) {
            auto a = measured.allocation_summary();
            line.push_str(rstd::format("  allocations: {} requests, {} grows, {} bytes requested\n"
                                       "  live bytes: {} -> {}, peak {}; after cleanup: {}\n",
                                       allocation_value_text(a.allocations),
                                       allocation_value_text(a.grows),
                                       allocation_value_text(a.requested_bytes),
                                       allocation_value_text(a.before_live),
                                       allocation_value_text(a.after_live),
                                       allocation_value_text(a.peak_live),
                                       allocation_value_text(a.cleanup_live))
                              .as_str());
        } else
            line.push_str(
                rstd::format("  allocations unavailable: {}\n", measured.allocation_unavailable)
                    .as_str());
        for (const auto& metric : measured.metrics) {
            auto unit = metric.unit == DiagnosticUnit::Count   ? "count"_str
                        : metric.unit == DiagnosticUnit::Bytes ? "bytes"_str
                                                               : "ratio"_str;
            line.push_str(
                rstd::format("  {} [{}; {}; {}]: ", metric.name, unit, metric.scope, metric.source)
                    .as_str());
            if (metric.value.is_Count())
                line.push_str(rstd::format("{}\n", metric.value.as_Count().value).as_str());
            else if (metric.value.is_Ratio())
                line.push_str(rstd::format("{}/{}\n",
                                           metric.value.as_Ratio().numerator,
                                           metric.value.as_Ratio().denominator)
                                  .as_str());
            else if (metric.value.is_Unavailable())
                line.push_str(
                    rstd::format("unavailable: {}\n", metric.value.as_Unavailable().reason)
                        .as_str());
            else
                line.push_str("overflow\n"_str);
        }
    } else if (result.outcome.is_Skipped()) {
        line = rstd::format(
            "{} skipped: {}\n", result.descriptor.id, result.outcome.as_Skipped().reason);
    } else {
        line = rstd::format("{} failed\n", result.descriptor.id);
        for (const auto& error : result.outcome.as_Failed().errors)
            line.push_str(rstd::format("  {}\n", error).as_str());
    }
    auto output  = rstd::io::stdout();
    auto written = rstd::io::write_all(output, line.as_str().as_bytes());
    if (written.is_err())
        return Err(rstd::format("cannot write benchmark report: {}", written.unwrap_err()));
    return Ok(empty {});
}
