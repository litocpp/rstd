module rstd.bench;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace rstd::bench;

auto DiagnosticContext::begin() -> Result<empty, String> {
    if (tracker_.begin_window().is_err()) return Err("cannot begin allocation window"_Str);
    return Ok(empty {});
}

auto DiagnosticContext::end() -> Result<rstd::alloc::AllocationWindow, String> {
    auto result = tracker_.end_window();
    if (result.is_err()) return Err("invalid allocation window accounting"_Str);
    return Ok(*result);
}

auto valid_allocation_stats(const rstd::alloc::AllocationStats& stats) -> bool {
    return ! stats.overflow && stats.live_bytes <= stats.peak_live_bytes;
}

auto allocation_counters_advance(const rstd::alloc::AllocationStats& a,
                                 const rstd::alloc::AllocationStats& b) -> bool {
    return a.allocations <= b.allocations && a.deallocations <= b.deallocations &&
           a.grows <= b.grows && a.shrinks <= b.shrinks && a.failures <= b.failures &&
           a.requested_bytes <= b.requested_bytes && a.peak_live_bytes <= b.peak_live_bytes;
}

auto valid_allocation_diagnostic(const AllocationDiagnostic& a) -> bool {
    return valid_allocation_stats(a.operation.before) &&
           valid_allocation_stats(a.operation.after) && valid_allocation_stats(a.lifetime) &&
           allocation_counters_advance(a.operation.before, a.operation.after) &&
           allocation_counters_advance(a.operation.after, a.lifetime) &&
           a.operation.peak_live_bytes >= a.operation.before.live_bytes &&
           a.operation.peak_live_bytes >= a.operation.after.live_bytes &&
           a.operation.peak_live_bytes <= a.operation.after.peak_live_bytes;
}

auto DiagnosticMeasurement::allocation_summary() const -> AllocationSummary {
    auto missing = [&] {
        if (allocations.is_none())
            return DiagnosticValue::Unavailable(allocation_unavailable.clone());
        if (iterations == u64())
            return DiagnosticValue::Unavailable("zero diagnostic operations"_Str);
        if (allocations->operation.before.overflow || allocations->operation.after.overflow ||
            allocations->lifetime.overflow)
            return DiagnosticValue::Overflow();
        return DiagnosticValue::Unavailable("invalid allocation accounting"_Str);
    };
    if (allocations.is_none() || iterations == u64() || ! valid_allocation_diagnostic(*allocations))
        return { missing(), missing(), missing(), missing(), missing(), missing(),
                 missing(), missing(), missing(), missing(), missing(), missing() };
    const auto& a        = *allocations;
    const auto& before   = a.operation.before;
    const auto& after    = a.operation.after;
    auto        requests = after.allocations - before.allocations;
    auto        bytes    = after.requested_bytes - before.requested_bytes;
    return { DiagnosticValue::Count(requests),
             DiagnosticValue::Count(after.deallocations - before.deallocations),
             DiagnosticValue::Count(after.grows - before.grows),
             DiagnosticValue::Count(after.shrinks - before.shrinks),
             DiagnosticValue::Count(after.failures - before.failures),
             DiagnosticValue::Count(bytes),
             DiagnosticValue::Ratio(requests, iterations),
             DiagnosticValue::Ratio(bytes, iterations),
             DiagnosticValue::Count(before.live_bytes),
             DiagnosticValue::Count(after.live_bytes),
             DiagnosticValue::Count(a.operation.peak_live_bytes),
             DiagnosticValue::Count(a.lifetime.live_bytes) };
}

auto DiagnosticMeasurement::validate() const -> Result<empty, String> {
    if (iterations == u64() || work.validate().is_err() || work.unit.is_empty())
        return Err("invalid diagnostic work amount"_Str);
    if (scope != MeasurementScope::Repeated && scope != MeasurementScope::Batched &&
        scope != MeasurementScope::BatchedRef)
        return Err("invalid diagnostic scope"_Str);
    if ((scope == MeasurementScope::Repeated) != batch_size.is_none() ||
        (batch_size.is_some() && *batch_size == usize()))
        return Err("diagnostic scope and batch size disagree"_Str);
    for (usize i; i < metrics.len(); ++i) {
        const auto& metric = metrics[i];
        if (metric.name.is_empty() || metric.scope.is_empty() || metric.source.is_empty())
            return Err("diagnostic metric requires name, scope and source"_Str);
        if (metric.unit != DiagnosticUnit::Count && metric.unit != DiagnosticUnit::Bytes &&
            metric.unit != DiagnosticUnit::Ratio)
            return Err("invalid diagnostic unit"_Str);
        if (metric.direction != MetricDirection::Lower &&
            metric.direction != MetricDirection::Higher &&
            metric.direction != MetricDirection::Neutral)
            return Err("invalid diagnostic direction"_Str);
        for (usize j; j < i; ++j)
            if (metrics[j].name == metric.name && metrics[j].scope == metric.scope)
                return Err("duplicate diagnostic metric"_Str);
        if (metric.value.is_Unavailable()) {
            if (metric.value.as_Unavailable().reason.is_empty())
                return Err("unavailable diagnostic requires a reason"_Str);
        } else if (metric.value.is_Ratio()) {
            if (metric.unit != DiagnosticUnit::Ratio ||
                metric.value.as_Ratio().denominator == u64())
                return Err("invalid diagnostic ratio"_Str);
        } else if (metric.value.is_Count() && metric.unit == DiagnosticUnit::Ratio)
            return Err("diagnostic ratio requires raw numerator and denominator"_Str);
    }
    if (allocations.is_none()) {
        if (allocation_unavailable.is_empty())
            return Err("missing allocation availability reason"_Str);
    } else {
        if (! allocation_unavailable.is_empty())
            return Err("conflicting allocation availability"_Str);
        const auto& a = *allocations;
        if (! valid_allocation_diagnostic(a))
            return Err("invalid diagnostic allocation accounting"_Str);
        if (a.lifetime.live_bytes != u64())
            return Err("tracked allocations outlived diagnostic session"_Str);
    }
    return Ok(empty {});
}
