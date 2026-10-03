module;
#include <rstd/macro.hpp>

module rstd_benches;
import rstd.bench;
import rstd.json;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace rstd::bench;
using rstd::json::Value;
using rstd::json::Number;

auto integer(u64 value) -> Value {
    return Value::Number(Number::from_u64(value));
}
auto json_text(ref<str> value) -> Value {
    return Value::String(String::make(value));
}
auto optional(Option<u64> value) -> Value {
    return value.is_some() ? integer(*value) : Value::Null();
}
auto optional_real(Option<f64> value) -> Value {
    if (value.is_none()) return Value::Null();
    auto number = Number::from_f64(*value);
    return number.is_some() ? Value::Number(*number) : Value::Null();
}
auto member(const Value& value, ref<str> name) -> Result<ref<Value>, String> {
    auto found = value.get(name);
    if (found.is_none()) return Err(rstd::format("missing report field: {}", name));
    return Ok(*found);
}
template<typename T>
auto field(const Value& value, ref<str> name) -> Result<T, String> {
    auto child   = rstd_try(member(value, name));
    auto decoded = rstd::json::decode_value<T>(*child);
    if (decoded.is_err())
        return Err(rstd::format("invalid report field {}: {}", name, decoded.unwrap_err()));
    return Ok(rstd::move(decoded).unwrap());
}
auto read_array(const Value& value, ref<str> name) -> Result<ref<rstd::json::Array>, String> {
    auto child = rstd_try(member(value, name));
    auto items = child->as_array();
    if (items.is_none()) return Err(rstd::format("report field {} must be an array", name));
    return Ok(*items);
}

auto parameter_value(const ParameterValue& value) -> Value {
    Value object;
    if (value.is_Unsigned()) {
        object["kind"_str]  = json_text("unsigned"_str);
        object["value"_str] = integer(value.as_Unsigned().value);
    } else if (value.is_Signed()) {
        object["kind"_str]  = json_text("signed"_str);
        object["value"_str] = Value::Number(Number::from_i64(value.as_Signed().value));
    } else if (value.is_Real()) {
        object["kind"_str]  = json_text("real"_str);
        object["value"_str] = optional_real(Some(f64(value.as_Real().value)));
    } else if (value.is_Boolean()) {
        object["kind"_str]  = json_text("boolean"_str);
        object["value"_str] = Value::Bool(value.as_Boolean().value);
    } else {
        object["kind"_str]  = json_text("text"_str);
        object["value"_str] = json_text(value.as_Text().value.as_str());
    }
    return object;
}

auto read_parameter(const Value& value) -> Result<ParameterValue, String> {
    auto kind = rstd_try(field<String>(value, "kind"_str));
    if (kind == "unsigned"_str)
        return Ok(ParameterValue::Unsigned(rstd_try(field<u64>(value, "value"_str))));
    if (kind == "signed"_str)
        return Ok(ParameterValue::Signed(rstd_try(field<i64>(value, "value"_str))));
    if (kind == "real"_str)
        return Ok(ParameterValue::Real(rstd_try(field<f64>(value, "value"_str))));
    if (kind == "boolean"_str)
        return Ok(ParameterValue::Boolean(rstd_try(field<bool>(value, "value"_str))));
    if (kind == "text"_str)
        return Ok(ParameterValue::Text(rstd_try(field<String>(value, "value"_str))));
    return Err("unknown parameter kind"_Str);
}

auto descriptor_value(const CaseDescriptor& value) -> Value {
    Value object;
    object["id"_str]               = json_text(value.id.as_str());
    object["name"_str]             = json_text(value.name.as_str());
    object["suite"_str]            = json_text(value.suite.as_str());
    object["revision"_str]         = json_text(value.revision.as_str());
    object["implementation"_str]   = json_text(value.implementation.as_str());
    object["quick_iterations"_str] = integer(value.quick_iterations);
    auto parameters                = rstd::json::Array::make();
    for (const auto& parameter : value.parameters) {
        auto entry        = parameter_value(parameter.value);
        entry["name"_str] = json_text(parameter.name.as_str());
        parameters.push(rstd::move(entry));
    }
    object["parameters"_str] = Value::Array(rstd::move(parameters));
    Value dataset;
    if (value.dataset.is_some()) {
        const auto& data                = *value.dataset;
        dataset["id"_str]               = json_text(data.id.as_str());
        dataset["revision"_str]         = json_text(data.revision.as_str());
        dataset["digest_algorithm"_str] = json_text(data.digest_algorithm.as_str());
        dataset["digest"_str]           = json_text(data.digest.as_str());
        dataset["input_bytes"_str]      = integer(data.input_bytes);
        dataset["seed"_str]             = optional(data.seed);
    }
    object["dataset"_str] = rstd::move(dataset);
    return object;
}

auto read_descriptor(const Value& object) -> Result<CaseDescriptor, String> {
    CaseDescriptor value;
    value.id               = rstd_try(field<String>(object, "id"_str));
    value.name             = rstd_try(field<String>(object, "name"_str));
    value.suite            = rstd_try(field<String>(object, "suite"_str));
    value.revision         = rstd_try(field<String>(object, "revision"_str));
    value.implementation   = rstd_try(field<String>(object, "implementation"_str));
    value.quick_iterations = rstd_try(field<u64>(object, "quick_iterations"_str));
    for (const auto& entry : *rstd_try(read_array(object, "parameters"_str)))
        value.parameters.push(
            { rstd_try(field<String>(entry, "name"_str)), rstd_try(read_parameter(entry)) });
    auto dataset = rstd_try(member(object, "dataset"_str));
    if (! dataset->is_Null()) {
        value.dataset =
            Some(DatasetDescriptor { rstd_try(field<String>(*dataset, "id"_str)),
                                     rstd_try(field<String>(*dataset, "revision"_str)),
                                     rstd_try(field<String>(*dataset, "digest_algorithm"_str)),
                                     rstd_try(field<String>(*dataset, "digest"_str)),
                                     rstd_try(field<u64>(*dataset, "input_bytes"_str)),
                                     rstd_try(field<Option<u64>>(*dataset, "seed"_str)) });
    }
    rstd_try(value.validate());
    return Ok(rstd::move(value));
}

auto config_value(const BenchConfig& value) -> Value {
    Value object;
    object["epochs"_str]            = integer(u64(value.epochs.to_primitive()));
    object["min_epoch_ns"_str]      = integer(u64(value.min_epoch_time.as_nanos().to_primitive()));
    object["max_epoch_ns"_str]      = integer(u64(value.max_epoch_time.as_nanos().to_primitive()));
    object["min_iterations"_str]    = integer(value.min_epoch_iterations);
    object["exact_iterations"_str]  = optional(value.exact_epoch_iterations);
    object["warmup_iterations"_str] = integer(value.warmup_iterations);
    object["resolution_multiple"_str] =
        integer(u64(value.clock_resolution_multiple.to_primitive()));
    object["jitter_seed"_str]  = integer(value.jitter_seed);
    object["counter_mode"_str] = json_text(value.counter_mode.is_Disabled()   ? "disabled"_str
                                           : value.counter_mode.is_Required() ? "required"_str
                                                                              : "auto"_str);
    return object;
}

auto read_config(const Value& object) -> Result<BenchConfig, String> {
    BenchConfig value;
    value.epochs = rstd_try(field<usize>(object, "epochs"_str));
    value.min_epoch_time =
        rstd::time::Duration::from_nanos(rstd_try(field<u64>(object, "min_epoch_ns"_str)));
    value.max_epoch_time =
        rstd::time::Duration::from_nanos(rstd_try(field<u64>(object, "max_epoch_ns"_str)));
    value.min_epoch_iterations      = rstd_try(field<u64>(object, "min_iterations"_str));
    value.exact_epoch_iterations    = rstd_try(field<Option<u64>>(object, "exact_iterations"_str));
    value.warmup_iterations         = rstd_try(field<u64>(object, "warmup_iterations"_str));
    value.clock_resolution_multiple = rstd_try(field<usize>(object, "resolution_multiple"_str));
    value.jitter_seed               = rstd_try(field<u64>(object, "jitter_seed"_str));
    auto mode                       = rstd_try(field<String>(object, "counter_mode"_str));
    if (mode == "disabled"_str)
        value.counter_mode = CounterMode::Disabled();
    else if (mode == "auto"_str)
        value.counter_mode = CounterMode::Auto();
    else if (mode == "required"_str)
        value.counter_mode = CounterMode::Required();
    else
        return Err("invalid counter mode"_Str);
    auto valid = value.validate();
    if (valid.is_err())
        return Err(describe_error(BenchError::InvalidConfig(rstd::move(valid).unwrap_err())));
    return Ok(rstd::move(value));
}

auto counters_value(const CounterSet& value) -> Value {
    Value object;
    object["page_faults"_str]      = optional(value.page_faults);
    object["cycles"_str]           = optional(value.cpu_cycles);
    object["context_switches"_str] = optional(value.context_switches);
    object["instructions"_str]     = optional(value.instructions);
    object["branches"_str]         = optional(value.branch_instructions);
    object["branch_misses"_str]    = optional(value.branch_misses);
    return object;
}

auto read_counters(const Value& object) -> Result<CounterSet, String> {
    return Ok(CounterSet { rstd_try(field<Option<u64>>(object, "page_faults"_str)),
                           rstd_try(field<Option<u64>>(object, "cycles"_str)),
                           rstd_try(field<Option<u64>>(object, "context_switches"_str)),
                           rstd_try(field<Option<u64>>(object, "instructions"_str)),
                           rstd_try(field<Option<u64>>(object, "branches"_str)),
                           rstd_try(field<Option<u64>>(object, "branch_misses"_str)) });
}

auto metrics_value(const Vec<MetricSample>& values) -> Value {
    auto array = rstd::json::Array::make();
    for (const auto& metric : values) {
        Value entry;
        entry["name"_str] = json_text(metric.name.as_str());
        entry["unit"_str] = json_text(metric.unit == MetricUnit::Bytes   ? "bytes"_str
                                      : metric.unit == MetricUnit::Count ? "count"_str
                                                                         : "ns"_str);
        entry["aggregation"_str] =
            json_text(metric.aggregation == MetricAggregation::Sum       ? "sum"_str
                      : metric.aggregation == MetricAggregation::Maximum ? "max"_str
                                                                         : "last"_str);
        entry["direction"_str] =
            json_text(metric.direction == MetricDirection::Lower    ? "lower"_str
                      : metric.direction == MetricDirection::Higher ? "higher"_str
                                                                    : "neutral"_str);
        entry["value"_str] = integer(metric.value);
        array.push(rstd::move(entry));
    }
    return Value::Array(rstd::move(array));
}

auto read_metrics(const Value& object) -> Result<Vec<MetricSample>, String> {
    Vec<MetricSample> metrics;
    for (const auto& entry : *rstd_try(read_array(object, "metrics"_str))) {
        MetricSample sample;
        sample.name  = rstd_try(field<String>(entry, "name"_str));
        sample.value = rstd_try(field<u64>(entry, "value"_str));
        auto unit    = rstd_try(field<String>(entry, "unit"_str));
        if (unit == "bytes"_str)
            sample.unit = MetricUnit::Bytes;
        else if (unit == "count"_str)
            sample.unit = MetricUnit::Count;
        else if (unit == "ns"_str)
            sample.unit = MetricUnit::Nanoseconds;
        else
            return Err("invalid metric unit"_Str);
        auto aggregation = rstd_try(field<String>(entry, "aggregation"_str));
        if (aggregation == "sum"_str)
            sample.aggregation = MetricAggregation::Sum;
        else if (aggregation == "max"_str)
            sample.aggregation = MetricAggregation::Maximum;
        else if (aggregation == "last"_str)
            sample.aggregation = MetricAggregation::Last;
        else
            return Err("invalid metric aggregation"_Str);
        auto direction = rstd_try(field<String>(entry, "direction"_str));
        if (direction == "lower"_str)
            sample.direction = MetricDirection::Lower;
        else if (direction == "higher"_str)
            sample.direction = MetricDirection::Higher;
        else if (direction == "neutral"_str)
            sample.direction = MetricDirection::Neutral;
        else
            return Err("invalid metric direction"_Str);
        if (sample.name.is_empty()) return Err("empty metric name"_Str);
        for (const auto& other : metrics)
            if (other.name == sample.name) return Err("duplicate metric name"_Str);
        metrics.push(rstd::move(sample));
    }
    return Ok(rstd::move(metrics));
}

auto measurement_value(const BenchmarkResult& measured) -> Value {
    Value object;
    object["name"_str]      = json_text(measured.name());
    object["collector"_str] = json_text(measured.collector_identity());
    object["scope"_str] =
        json_text(measured.scope() == MeasurementScope::Repeated  ? "repeated"_str
                  : measured.scope() == MeasurementScope::Batched ? "owned-input"_str
                                                                  : "borrowed-input"_str);
    object["batch_max_items"_str] = measured.batch_size().is_some()
                                        ? integer(u64(measured.batch_size()->to_primitive()))
                                        : Value::Null();
    object["config"_str]          = config_value(measured.config());
    object["clock_resolution_ns"_str] =
        integer(u64(measured.clock_resolution().as_nanos().to_primitive()));
    const auto& work                  = measured.run_config();
    object["unit"_str]                = json_text(work.unit.as_str());
    object["units_per_iteration"_str] = optional_real(Some(f64(work.batch)));
    object["items_per_iteration"_str] = integer(work.items_per_iteration);
    object["bytes_per_iteration"_str] = integer(work.bytes_per_iteration);
    const auto& availability          = measured.counter_availability();
    object["counter_availability"_str] =
        json_text(availability.is_Disabled()    ? "disabled"_str
                  : availability.is_Available() ? "available"_str
                                                : "unavailable"_str);
    object["counter_mask"_str] = availability.is_Available()
                                     ? integer(u64(availability.as_Available().mask.to_primitive()))
                                     : Value::Null();
    object["counter_error"_str] = availability.is_Unavailable()
                                      ? Value::Number(Number::from_i64(
                                            i64(availability.as_Unavailable().code.to_primitive())))
                                      : Value::Null();
    auto epochs                 = rstd::json::Array::make();
    for (const auto& epoch : measured.measurements()) {
        Value entry;
        entry["iterations"_str] = integer(epoch.iterations);
        entry["elapsed_ns"_str] = integer(u64(epoch.elapsed.as_nanos().to_primitive()));
        entry["counters"_str]   = counters_value(epoch.counters);
        entry["metrics"_str]    = metrics_value(epoch.metrics);
        epochs.push(rstd::move(entry));
    }
    object["epochs"_str] = Value::Array(rstd::move(epochs));
    auto  summary        = measured.summary();
    Value statistics;
    statistics["median_ns_per_op"_str] =
        optional_real(Some(summary.median_ns_per_unit * work.batch));
    statistics["median_items_per_second"_str] = optional_real(summary.median_items_per_second);
    statistics["median_bytes_per_second"_str] = optional_real(summary.median_bytes_per_second);
    statistics["mdape"_str] = optional_real(Some(f64(summary.median_absolute_percentage_error)));
    object["summary"_str]   = rstd::move(statistics);
    return object;
}

auto read_measurement(const Value& object) -> Result<BenchmarkResult, String> {
    auto name      = rstd_try(field<String>(object, "name"_str));
    auto collector = rstd_try(field<String>(object, "collector"_str));
    if (collector.is_empty()) return Err("missing collector identity"_Str);
    auto             scope_name = rstd_try(field<String>(object, "scope"_str));
    MeasurementScope scope;
    if (scope_name == "repeated"_str)
        scope = MeasurementScope::Repeated;
    else if (scope_name == "owned-input"_str)
        scope = MeasurementScope::Batched;
    else if (scope_name == "borrowed-input"_str)
        scope = MeasurementScope::BatchedRef;
    else
        return Err("invalid measurement scope"_Str);
    auto batch = rstd_try(field<Option<usize>>(object, "batch_max_items"_str));
    if ((scope == MeasurementScope::Repeated) != batch.is_none() ||
        (batch.is_some() && *batch == usize()))
        return Err("scope and batch size disagree"_Str);
    auto config     = rstd_try(read_config(*rstd_try(member(object, "config"_str))));
    auto resolution = rstd_try(field<u64>(object, "clock_resolution_ns"_str));
    if (resolution == u64()) return Err("clock resolution must be nonzero"_Str);
    RunConfig work { rstd_try(field<String>(object, "unit"_str)),
                     rstd_try(field<f64>(object, "units_per_iteration"_str)),
                     rstd_try(field<u64>(object, "items_per_iteration"_str)),
                     rstd_try(field<u64>(object, "bytes_per_iteration"_str)) };
    auto      valid = work.validate();
    if (valid.is_err()) return Err("invalid work amount"_Str);
    auto                state        = rstd_try(field<String>(object, "counter_availability"_str));
    CounterAvailability availability = CounterAvailability::Disabled();
    if (state == "available"_str)
        availability =
            CounterAvailability::Available(rstd_try(field<u32>(object, "counter_mask"_str)));
    else if (state == "unavailable"_str)
        availability =
            CounterAvailability::Unavailable(rstd_try(field<i32>(object, "counter_error"_str)));
    else if (state != "disabled"_str)
        return Err("invalid counter availability"_Str);
    Vec<EpochMeasurement> epochs;
    for (const auto& entry : *rstd_try(read_array(object, "epochs"_str))) {
        auto elapsed    = rstd_try(field<u64>(entry, "elapsed_ns"_str));
        auto iterations = rstd_try(field<u64>(entry, "iterations"_str));
        if (elapsed == u64() || iterations == u64())
            return Err("epoch duration and iterations must be nonzero"_Str);
        epochs.push({ rstd::time::Duration::from_nanos(elapsed),
                      iterations,
                      rstd_try(read_counters(*rstd_try(member(entry, "counters"_str)))),
                      rstd_try(read_metrics(entry)) });
    }
    if (epochs.len() != config.epochs) return Err("epoch count differs from configuration"_Str);
    return Ok(BenchmarkResult(rstd::move(name),
                              rstd::move(work),
                              config,
                              rstd::time::Duration::from_nanos(resolution),
                              rstd::move(availability),
                              rstd::move(epochs),
                              scope,
                              batch,
                              rstd::move(collector)));
}

auto allocation_stats_value(const rstd::alloc::AllocationStats& stats) -> Value {
    Value value;
    value["allocations"_str]     = integer(stats.allocations);
    value["deallocations"_str]   = integer(stats.deallocations);
    value["grows"_str]           = integer(stats.grows);
    value["shrinks"_str]         = integer(stats.shrinks);
    value["failures"_str]        = integer(stats.failures);
    value["requested_bytes"_str] = integer(stats.requested_bytes);
    value["live_bytes"_str]      = integer(stats.live_bytes);
    value["peak_live_bytes"_str] = integer(stats.peak_live_bytes);
    value["overflow"_str]        = Value::Bool(stats.overflow);
    return value;
}

auto read_allocation_stats(const Value& value) -> Result<rstd::alloc::AllocationStats, String> {
    return Ok(rstd::alloc::AllocationStats { rstd_try(field<u64>(value, "allocations"_str)),
                                             rstd_try(field<u64>(value, "deallocations"_str)),
                                             rstd_try(field<u64>(value, "grows"_str)),
                                             rstd_try(field<u64>(value, "shrinks"_str)),
                                             rstd_try(field<u64>(value, "failures"_str)),
                                             rstd_try(field<u64>(value, "requested_bytes"_str)),
                                             rstd_try(field<u64>(value, "live_bytes"_str)),
                                             rstd_try(field<u64>(value, "peak_live_bytes"_str)),
                                             rstd_try(field<bool>(value, "overflow"_str)) });
}

auto diagnostic_value(const DiagnosticMeasurement& measurement) -> Value {
    Value object;
    object["iterations"_str]          = integer(measurement.iterations);
    object["unit"_str]                = json_text(measurement.work.unit.as_str());
    object["units_per_iteration"_str] = optional_real(Some(f64(measurement.work.batch)));
    object["items_per_iteration"_str] = integer(measurement.work.items_per_iteration);
    object["bytes_per_iteration"_str] = integer(measurement.work.bytes_per_iteration);
    object["scope"_str] =
        json_text(measurement.scope == MeasurementScope::Repeated  ? "repeated"_str
                  : measurement.scope == MeasurementScope::Batched ? "owned-input"_str
                                                                   : "borrowed-input"_str);
    object["batch_max_items"_str] = measurement.batch_size.is_some()
                                        ? integer(u64(measurement.batch_size->to_primitive()))
                                        : Value::Null();
    object["allocation_unavailable"_str] = json_text(measurement.allocation_unavailable.as_str());
    Value allocations;
    if (measurement.allocations.is_some()) {
        const auto& a                             = *measurement.allocations;
        allocations["before"_str]                 = allocation_stats_value(a.operation.before);
        allocations["after"_str]                  = allocation_stats_value(a.operation.after);
        allocations["window_peak_live_bytes"_str] = integer(a.operation.peak_live_bytes);
        allocations["lifetime"_str]               = allocation_stats_value(a.lifetime);
    }
    object["allocations"_str] = rstd::move(allocations);
    auto metrics              = rstd::json::Array::make();
    for (const auto& metric : measurement.metrics) {
        Value entry;
        entry["name"_str] = json_text(metric.name.as_str());
        entry["unit"_str] = json_text(metric.unit == DiagnosticUnit::Count   ? "count"_str
                                      : metric.unit == DiagnosticUnit::Bytes ? "bytes"_str
                                                                             : "ratio"_str);
        entry["direction"_str] =
            json_text(metric.direction == MetricDirection::Lower    ? "lower"_str
                      : metric.direction == MetricDirection::Higher ? "higher"_str
                                                                    : "neutral"_str);
        entry["scope"_str]  = json_text(metric.scope.as_str());
        entry["source"_str] = json_text(metric.source.as_str());
        if (metric.value.is_Count()) {
            entry["status"_str] = json_text("count"_str);
            entry["value"_str]  = integer(metric.value.as_Count().value);
        } else if (metric.value.is_Ratio()) {
            entry["status"_str]      = json_text("ratio"_str);
            entry["numerator"_str]   = integer(metric.value.as_Ratio().numerator);
            entry["denominator"_str] = integer(metric.value.as_Ratio().denominator);
        } else if (metric.value.is_Unavailable()) {
            entry["status"_str] = json_text("unavailable"_str);
            entry["reason"_str] = json_text(metric.value.as_Unavailable().reason.as_str());
        } else
            entry["status"_str] = json_text("overflow"_str);
        metrics.push(rstd::move(entry));
    }
    object["metrics"_str] = Value::Array(rstd::move(metrics));
    return object;
}

auto read_diagnostic(const Value& object) -> Result<DiagnosticMeasurement, String> {
    DiagnosticMeasurement value;
    value.iterations = rstd_try(field<u64>(object, "iterations"_str));
    value.work       = { rstd_try(field<String>(object, "unit"_str)),
                         rstd_try(field<f64>(object, "units_per_iteration"_str)),
                         rstd_try(field<u64>(object, "items_per_iteration"_str)),
                         rstd_try(field<u64>(object, "bytes_per_iteration"_str)) };
    auto scope       = rstd_try(field<String>(object, "scope"_str));
    if (scope == "repeated"_str)
        value.scope = MeasurementScope::Repeated;
    else if (scope == "owned-input"_str)
        value.scope = MeasurementScope::Batched;
    else if (scope == "borrowed-input"_str)
        value.scope = MeasurementScope::BatchedRef;
    else
        return Err("invalid diagnostic scope"_Str);
    value.batch_size             = rstd_try(field<Option<usize>>(object, "batch_max_items"_str));
    value.allocation_unavailable = rstd_try(field<String>(object, "allocation_unavailable"_str));
    auto allocations             = rstd_try(member(object, "allocations"_str));
    if (! allocations->is_Null()) {
        value.allocations = Some(AllocationDiagnostic {
            { rstd_try(read_allocation_stats(*rstd_try(member(*allocations, "before"_str)))),
              rstd_try(read_allocation_stats(*rstd_try(member(*allocations, "after"_str)))),
              rstd_try(field<u64>(*allocations, "window_peak_live_bytes"_str)) },
            rstd_try(read_allocation_stats(*rstd_try(member(*allocations, "lifetime"_str)))) });
    }
    for (const auto& entry : *rstd_try(read_array(object, "metrics"_str))) {
        auto           unit = rstd_try(field<String>(entry, "unit"_str));
        DiagnosticUnit parsed_unit;
        if (unit == "count"_str)
            parsed_unit = DiagnosticUnit::Count;
        else if (unit == "bytes"_str)
            parsed_unit = DiagnosticUnit::Bytes;
        else if (unit == "ratio"_str)
            parsed_unit = DiagnosticUnit::Ratio;
        else
            return Err("invalid diagnostic unit"_Str);
        auto            direction = rstd_try(field<String>(entry, "direction"_str));
        MetricDirection parsed_direction;
        if (direction == "lower"_str)
            parsed_direction = MetricDirection::Lower;
        else if (direction == "higher"_str)
            parsed_direction = MetricDirection::Higher;
        else if (direction == "neutral"_str)
            parsed_direction = MetricDirection::Neutral;
        else
            return Err("invalid diagnostic direction"_Str);
        auto status       = rstd_try(field<String>(entry, "status"_str));
        auto metric_value = DiagnosticValue::Overflow();
        if (status == "count"_str)
            metric_value = DiagnosticValue::Count(rstd_try(field<u64>(entry, "value"_str)));
        else if (status == "ratio"_str)
            metric_value = DiagnosticValue::Ratio(rstd_try(field<u64>(entry, "numerator"_str)),
                                                  rstd_try(field<u64>(entry, "denominator"_str)));
        else if (status == "unavailable"_str)
            metric_value =
                DiagnosticValue::Unavailable(rstd_try(field<String>(entry, "reason"_str)));
        else if (status != "overflow"_str)
            return Err("invalid diagnostic status"_Str);
        value.metrics.push({ rstd_try(field<String>(entry, "name"_str)),
                             parsed_unit,
                             parsed_direction,
                             rstd_try(field<String>(entry, "scope"_str)),
                             rstd_try(field<String>(entry, "source"_str)),
                             rstd::move(metric_value) });
    }
    rstd_try(value.validate());
    return Ok(rstd::move(value));
}

auto rstd_bench::report::to_value(const RunReport& report) -> Result<Value, String> {
    rstd_try(report.validate());
    for (const auto& result : report.results) {
        if (! result.outcome.is_Passed()) continue;
        const auto& measured = result.outcome.as_Passed().measurement;
        if (measured.clock_resolution().as_nanos() > u128(u64::MAX.to_primitive()) ||
            measured.config().min_epoch_time.as_nanos() > u128(u64::MAX.to_primitive()) ||
            measured.config().max_epoch_time.as_nanos() > u128(u64::MAX.to_primitive()))
            return Err("benchmark duration exceeds JSON report range"_Str);
        for (const auto& epoch : measured.measurements())
            if (epoch.elapsed.as_nanos() > u128(u64::MAX.to_primitive()))
                return Err("epoch duration exceeds JSON report range"_Str);
    }
    Value object;
    object["format_version"_str] = integer(u64(3));
    object["mode"_str] =
        json_text(report.mode == RunMode::Timing ? "timing"_str : "diagnostic"_str);
    object["quick"_str]      = Value::Bool(report.quick);
    object["process_id"_str] = integer(u64(report.process_id.to_primitive()));
    auto environment         = rstd::json::Array::make();
    for (const auto& field : report.environment.fields) {
        Value entry;
        entry["name"_str] = json_text(field.name.as_str());
        entry["value"_str] =
            field.value.is_some() ? json_text(field.value->as_str()) : Value::Null();
        entry["source"_str] = json_text(field.source.as_str());
        environment.push(rstd::move(entry));
    }
    object["environment"_str] = Value::Array(rstd::move(environment));
    auto cases                = rstd::json::Array::make();
    for (const auto& result : report.results) {
        rstd_try(result.descriptor.validate());
        Value entry;
        entry["descriptor"_str] = descriptor_value(result.descriptor);
        entry["repetition"_str] = integer(result.repetition);
        if (result.outcome.is_Passed()) {
            entry["status"_str]      = json_text("ok"_str);
            entry["measurement"_str] = measurement_value(result.outcome.as_Passed().measurement);
        } else if (result.outcome.is_Diagnosed()) {
            entry["status"_str]     = json_text("diagnosed"_str);
            entry["diagnostic"_str] = diagnostic_value(result.outcome.as_Diagnosed().measurement);
        } else if (result.outcome.is_Skipped()) {
            entry["status"_str] = json_text("skipped"_str);
            entry["reason"_str] = json_text(result.outcome.as_Skipped().reason.as_str());
        } else {
            entry["status"_str] = json_text("failed"_str);
            auto errors         = rstd::json::Array::make();
            for (const auto& error : result.outcome.as_Failed().errors)
                errors.push(json_text(error.as_str()));
            entry["errors"_str] = Value::Array(rstd::move(errors));
        }
        cases.push(rstd::move(entry));
    }
    object["cases"_str] = Value::Array(rstd::move(cases));
    return Ok(rstd::move(object));
}

auto rstd_bench::report::from_value(const Value& value) -> Result<RunReport, String> {
    if (rstd_try(field<u64>(value, "format_version"_str)) != u64(3))
        return Err("unsupported benchmark report version; collect a new baseline"_Str);
    RunReport report;
    auto      mode = rstd_try(field<String>(value, "mode"_str));
    if (mode == "timing"_str)
        report.mode = RunMode::Timing;
    else if (mode == "diagnostic"_str)
        report.mode = RunMode::Diagnostic;
    else
        return Err("invalid report mode"_Str);
    report.quick      = rstd_try(field<bool>(value, "quick"_str));
    report.process_id = rstd_try(field<u32>(value, "process_id"_str));
    for (const auto& entry : *rstd_try(read_array(value, "environment"_str)))
        report.environment.fields.push({ rstd_try(field<String>(entry, "name"_str)),
                                         rstd_try(field<Option<String>>(entry, "value"_str)),
                                         rstd_try(field<String>(entry, "source"_str)) });
    rstd_try(report.environment.validate());
    for (const auto& entry : *rstd_try(read_array(value, "cases"_str))) {
        auto descriptor = rstd_try(read_descriptor(*rstd_try(member(entry, "descriptor"_str))));
        auto repetition = rstd_try(field<u64>(entry, "repetition"_str));
        for (const auto& existing : report.results)
            if (existing.descriptor.id == descriptor.id && existing.repetition == repetition)
                return Err("duplicate case repetition"_Str);
        auto status = rstd_try(field<String>(entry, "status"_str));
        if (status != "ok"_str && entry.get("measurement"_str).is_some())
            return Err("non-timing case contains timing measurement"_Str);
        if (status != "diagnosed"_str && entry.get("diagnostic"_str).is_some())
            return Err("non-diagnostic case contains diagnostic measurement"_Str);
        if (status == "ok"_str) {
            auto measurement =
                rstd_try(read_measurement(*rstd_try(member(entry, "measurement"_str))));
            if (measurement.name() != descriptor.id.as_str())
                return Err("measurement name differs from case id"_Str);
            report.results.push({ rstd::move(descriptor),
                                  CaseOutcome::Passed(rstd::move(measurement)),
                                  repetition });
        } else if (status == "diagnosed"_str) {
            report.results.push({ rstd::move(descriptor),
                                  CaseOutcome::Diagnosed(rstd_try(
                                      read_diagnostic(*rstd_try(member(entry, "diagnostic"_str))))),
                                  repetition });
        } else {
            if (entry.get("measurement"_str).is_some())
                return Err("failed or skipped case contains measurement"_Str);
            if (status == "skipped"_str)
                report.results.push(
                    { rstd::move(descriptor),
                      CaseOutcome::Skipped(rstd_try(field<String>(entry, "reason"_str))),
                      repetition });
            else if (status == "failed"_str) {
                auto errors = rstd_try(field<Vec<String>>(entry, "errors"_str));
                if (errors.is_empty()) return Err("failed case must contain errors"_Str);
                report.results.push({ rstd::move(descriptor),
                                      CaseOutcome::Failed(rstd::move(errors)),
                                      repetition });
            } else
                return Err("unknown case status"_Str);
        }
    }
    rstd_try(report.validate());
    return Ok(rstd::move(report));
}

auto rstd_bench::report::encode(const RunReport& report) -> Result<String, String> {
    return Ok(rstd::json::to_string(rstd_try(to_value(report))));
}

auto rstd_bench::report::decode(ref<str> input) -> Result<RunReport, String> {
    auto parsed = rstd::json::from_str(
        input, { .reject_duplicate_keys = true, .reject_integer_overflow = true });
    if (parsed.is_err())
        return Err(rstd::format("invalid benchmark report: {}", parsed.unwrap_err()));
    return from_value(*parsed);
}

auto rstd_bench::report::read(ref<rstd::path::Path> path) -> Result<RunReport, String> {
    auto content = rstd::fs::read_to_string(path);
    if (content.is_err())
        return Err(rstd::format("cannot read benchmark report: {}", content.unwrap_err()));
    return decode(content->as_str());
}
