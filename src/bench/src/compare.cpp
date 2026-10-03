module rstd.bench;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace rstd::bench;

auto parameter_equal(const ParameterValue& a, const ParameterValue& b) -> bool {
    if (a.is_Unsigned() && b.is_Unsigned()) return a.as_Unsigned().value == b.as_Unsigned().value;
    if (a.is_Signed() && b.is_Signed()) return a.as_Signed().value == b.as_Signed().value;
    if (a.is_Real() && b.is_Real()) return a.as_Real().value == b.as_Real().value;
    if (a.is_Boolean() && b.is_Boolean()) return a.as_Boolean().value == b.as_Boolean().value;
    if (a.is_Text() && b.is_Text()) return a.as_Text().value == b.as_Text().value;
    return false;
}

auto CaseDescriptor::same_workload(const CaseDescriptor& b) const -> bool {
    const auto& a = *this;
    if (a.id != b.id || a.suite != b.suite || a.revision != b.revision ||
        a.parameters.len() != b.parameters.len() || a.dataset.is_some() != b.dataset.is_some())
        return false;
    for (const auto& parameter : a.parameters) {
        bool found = false;
        for (const auto& other : b.parameters)
            if (parameter.name == other.name && parameter_equal(parameter.value, other.value))
                found = true;
        if (! found) return false;
    }
    if (a.dataset.is_some()) {
        const auto& x = *a.dataset;
        const auto& y = *b.dataset;
        if (x.digest.is_empty() || x.digest_algorithm.is_empty() || x.id != y.id ||
            x.revision != y.revision || x.digest_algorithm != y.digest_algorithm ||
            x.digest != y.digest || x.input_bytes != y.input_bytes || x.seed != y.seed)
            return false;
    }
    return true;
}

auto BenchmarkResult::same_measurement_config(const BenchmarkResult& b) const -> bool {
    const auto& a  = *this;
    const auto& x  = a.run_config();
    const auto& y  = b.run_config();
    const auto& ac = a.config();
    const auto& bc = b.config();
    return ! a.collector_identity().is_empty() &&
           a.collector_identity() == b.collector_identity() && a.scope() == b.scope() &&
           a.batch_size() == b.batch_size() && x.unit == y.unit && x.batch == y.batch &&
           x.items_per_iteration == y.items_per_iteration &&
           x.bytes_per_iteration == y.bytes_per_iteration && ac.epochs == bc.epochs &&
           ac.warmup_iterations == bc.warmup_iterations &&
           ac.exact_epoch_iterations == bc.exact_epoch_iterations &&
           ac.min_epoch_iterations == bc.min_epoch_iterations &&
           ac.min_epoch_time == bc.min_epoch_time && ac.max_epoch_time == bc.max_epoch_time &&
           ac.clock_resolution_multiple == bc.clock_resolution_multiple &&
           ac.jitter_seed == bc.jitter_seed &&
           ac.counter_mode.is_Disabled() == bc.counter_mode.is_Disabled() &&
           ac.counter_mode.is_Required() == bc.counter_mode.is_Required();
}

auto environment_value(const RunEnvironment& environment, ref<str> name) -> Option<ref<str>> {
    for (const auto& field : environment.fields)
        if (field.name == name && field.value.is_some() && ! field.value->is_empty())
            return Some(field.value->as_str());
    return None();
}

auto rstd::bench::compare(const RunReport& baseline,
                          const RunReport& candidate,
                          ComparisonMode   mode) -> ComparisonReport {
    ComparisonReport report { .mode = mode };
    auto             baseline_valid  = baseline.validate();
    auto             candidate_valid = candidate.validate();
    const ref<str> required[] { "cpu"_str,     "os"_str,           "compiler"_str, "target"_str,
                                "profile"_str, "optimization"_str, "lto"_str,      "exceptions"_str,
                                "rtti"_str,    "stdlib"_str };
    for (auto field : required) {
        auto a = environment_value(baseline.environment, field);
        auto b = environment_value(candidate.environment, field);
        if (a.is_none() || b.is_none() || *a != *b)
            report.environment_differences.push(String::make(field));
    }
    for (usize index; index < baseline.results.len(); ++index) {
        const auto& reference = baseline.results[index];
        bool        seen      = false;
        for (usize previous; previous < index; ++previous)
            if (baseline.results[previous].descriptor.id == reference.descriptor.id) seen = true;
        if (seen) continue;
        CaseComparison comparison { .id = reference.descriptor.id.clone() };
        if (baseline_valid.is_err()) comparison.reasons.push(baseline_valid.unwrap_err().clone());
        if (candidate_valid.is_err()) comparison.reasons.push(candidate_valid.unwrap_err().clone());
        Vec<f64> before, after;
        if (baseline.mode != RunMode::Timing || candidate.mode != RunMode::Timing)
            comparison.reasons.push("resource diagnostics are not timing baselines"_Str);
        if (baseline.quick || candidate.quick)
            comparison.reasons.push("quick runs are not performance baselines"_Str);
        if (mode == ComparisonMode::Strict && ! report.environment_differences.is_empty())
            comparison.reasons.push("environment missing or different"_Str);
        auto collect = [&](const RunReport& input, Vec<f64>& values) {
            for (const auto& current : input.results) {
                if (current.descriptor.id != reference.descriptor.id) continue;
                if (! reference.descriptor.same_workload(current.descriptor)) {
                    comparison.reasons.push("workload identity differs"_Str);
                    continue;
                }
                if (! reference.outcome.is_Passed() || ! current.outcome.is_Passed()) {
                    comparison.reasons.push("case has no timing measurement"_Str);
                    continue;
                }
                const auto& measured = current.outcome.as_Passed().measurement;
                if (! reference.outcome.as_Passed().measurement.same_measurement_config(measured)) {
                    comparison.reasons.push("measurement scope or configuration differs"_Str);
                    continue;
                }
                auto ns = measured.summary().median_ns_per_unit * measured.run_config().batch;
                if (! ns.is_finite() || ns <= f64())
                    comparison.reasons.push("invalid timing sample"_Str);
                else
                    values.push(rstd::move(ns));
            }
        };
        collect(baseline, before);
        collect(candidate, after);
        if (before.is_empty() || after.is_empty())
            comparison.reasons.push("missing timing samples"_Str);
        if (comparison.reasons.is_empty()) {
            comparison.baseline_ns_per_op  = Some(median(rstd::move(before)));
            comparison.candidate_ns_per_op = Some(median(rstd::move(after)));
            comparison.candidate_over_baseline =
                Some(*comparison.candidate_ns_per_op / *comparison.baseline_ns_per_op);
        }
        report.cases.push(rstd::move(comparison));
    }
    for (const auto& result : candidate.results) {
        bool seen = false;
        for (const auto& existing : report.cases)
            if (existing.id == result.descriptor.id) seen = true;
        if (! seen) {
            CaseComparison missing { .id = result.descriptor.id.clone() };
            missing.reasons.push("case absent from baseline"_Str);
            report.cases.push(rstd::move(missing));
        }
    }
    return report;
}
