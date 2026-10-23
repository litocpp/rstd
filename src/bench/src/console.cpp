module;
#include <rstd/macro.hpp>

module rstd.bench;
import rstd;
import :console;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace rstd::bench;
namespace bench = rstd::bench;

auto console::number(f64 value) -> String {
    if (! value.is_finite()) return "unavailable"_Str;
    auto magnitude = value.abs();
    if (magnitude == f64()) return "0"_Str;
    if (magnitude >= f64(9999.5) || magnitude < f64(0.0099995))
        return rstd::format("{:.3e}", value);
    if (magnitude >= f64(999.95)) return rstd::format("{:.0}", value);
    if (magnitude >= f64(99.995)) return rstd::format("{:.1}", value);
    if (magnitude >= f64(9.9995)) return rstd::format("{:.2}", value);
    if (magnitude >= f64(0.99995)) return rstd::format("{:.3}", value);
    if (magnitude >= f64(0.099995)) return rstd::format("{:.4}", value);
    return rstd::format("{:.5}", value);
}

template<rstd::size_t N>
auto scaled(f64 value, f64 base, const ref<str> (&units)[N], bool space = true) -> String {
    if (! value.is_finite()) return "unavailable"_Str;
    usize index;
    auto  threshold = base == f64(1024) ? f64(1023.5) : f64(999.95);
    while (index + usize(1) < usize(N) && value.abs() >= threshold) {
        value /= base;
        ++index;
    }
    return rstd::format(
        "{}{}{}", console::number(value), space ? " "_str : ""_str, units[index.to_primitive()]);
}

auto console::time(f64 ns) -> String {
    const ref<str> units[] { "ns"_str, "us"_str, "ms"_str, "s"_str };
    return scaled(ns, f64(1000), units);
}
auto console::bytes(f64 value) -> String {
    const ref<str> units[] { "B"_str,   "KiB"_str, "MiB"_str, "GiB"_str,
                             "TiB"_str, "PiB"_str, "EiB"_str };
    if (value >= f64() && value < f64(1023.5) && value == value.floor())
        return rstd::format("{:.0} B", value);
    return scaled(value, f64(1024), units);
}
auto console::byte_rate(f64 value) -> String {
    const ref<str> units[] { "B/s"_str,  "kB/s"_str, "MB/s"_str, "GB/s"_str,
                             "TB/s"_str, "PB/s"_str, "EB/s"_str };
    return scaled(value, f64(1000), units);
}
auto console::item_rate(f64 value) -> String {
    const ref<str> units[] { ""_str, "k"_str, "M"_str, "G"_str, "T"_str, "P"_str, "E"_str };
    return scaled(value, f64(1000), units, false);
}

auto console::escaped(ref<str> value) -> String {
    String result;
    for (auto code : value.chars()) {
        auto ch = static_cast<char32_t>(code.to_primitive());
        if (ch < U' ' || ch == U'\x7f' || (ch >= U'\x80' && ch <= U'\x9f') || ch == U'\u2028' ||
            ch == U'\u2029' || (ch >= U'\u202a' && ch <= U'\u202e') ||
            (ch >= U'\u2066' && ch <= U'\u2069'))
            result.push_str(rstd::format("\\u{{{:x}}}", u32(ch)).as_str());
        else
            result.push(code);
    }
    return result;
}

auto console::exact(const bench::DiagnosticValue& value) -> String {
    if (value.is_Count()) return rstd::format("{}", value.as_Count().value);
    if (value.is_Ratio())
        return rstd::format("{}/{}", value.as_Ratio().numerator, value.as_Ratio().denominator);
    if (value.is_Unavailable())
        return rstd::format("unavailable: {}", escaped(value.as_Unavailable().reason.as_str()));
    return "overflow"_Str;
}

auto diagnostic_value(const bench::DiagnosticValue& value, bench::DiagnosticUnit unit) -> String {
    if (value.is_Unavailable()) return "unavailable"_Str;
    if (value.is_Overflow()) return "overflow"_Str;
    if (value.is_Count()) {
        auto count = value.as_Count().value;
        if (unit != bench::DiagnosticUnit::Bytes) return rstd::format("{}", count);
        if (count < u64(1024)) return rstd::format("{} B", count);
        return console::bytes(f64(count.to_primitive()));
    }
    const auto& ratio = value.as_Ratio();
    if (ratio.denominator == u64()) return "unavailable"_Str;
    auto result = f64(ratio.numerator.to_primitive()) / f64(ratio.denominator.to_primitive());
    if (unit == bench::DiagnosticUnit::Bytes) return console::bytes(result);
    if (unit == bench::DiagnosticUnit::Ratio)
        return rstd::format("{}%", console::number(result * f64(100)));
    return console::number(result);
}

auto console_ascii(ref<str> value) -> bool {
    for (auto byte : value.as_bytes())
        if (byte >= u8(128)) return false;
    return true;
}

void spaces(String& output, usize count, char character = ' ') {
    for (usize i; i < count; ++i) output.push_ascii(character);
}
void cell(String& output, ref<str> value, usize width, bool left = false) {
    auto padding = value.len() < width ? width - value.len() : usize();
    if (! left) spaces(output, padding);
    output.push_str(value);
    if (left) spaces(output, padding);
}
void column(String& output, ref<str> value, usize width = usize(15)) {
    output.push_str("  "_str);
    cell(output, value, width);
}
void name_cell(String& output, ref<str> raw, usize width) {
    auto name = console::escaped(raw);
    if (name.len() > width || ! console_ascii(name.as_str())) {
        output.push_str(name.as_str());
        output.push_ascii('\n');
        spaces(output, width);
    } else
        cell(output, name.as_str(), width, true);
}
void divider(String& output, usize width) {
    spaces(output, width, '-');
    output.push_ascii('\n');
}
void SummaryRenderer::include(ref<str> name, ref<str> unit) {
    auto names = names_.max(console::escaped(name).len().min(usize(48)));
    auto units = units_.max(console::escaped(unit).len());
    if (names != names_ || units != units_) started_ = false;
    names_ = names;
    units_ = units;
}
auto SummaryRenderer::header() -> String {
    if (started_) return {};
    started_ = true;
    String output;
    cell(output, "case"_str, names_, true);
    column(output, "result"_str, values_);
    column(output, "unit"_str, units_);
    output.push_str("  status\n"_str);
    divider(output, names_ + values_ + units_ + usize(14));
    return output;
}
auto SummaryRenderer::render(const SummaryRow& row) -> Result<String, String> {
    if (row.name.is_empty() || row.unit.is_empty())
        return Err("summary row requires name and unit"_Str);
    if (row.value.is_Measured() && ! row.value.as_Measured().value.is_finite())
        return Err("summary value must be finite"_Str);
    include(row.name.as_str(), row.unit.as_str());
    auto output = header();
    name_cell(output, row.name.as_str(), names_);
    column(output,
           row.value.is_Measured() ? console::number(row.value.as_Measured().value).as_str()
                                   : "-"_str,
           values_);
    auto unit = console::escaped(row.unit.as_str());
    column(output, unit.as_str(), units_);
    if (row.value.is_Measured())
        output.push_str("  ok\n"_str);
    else {
        auto reason =
            console::escaped(row.value.is_Skipped() ? row.value.as_Skipped().reason.as_str()
                                                    : row.value.as_Failed().reason.as_str());
        output.push_str(rstd::format("  {}: {}\n",
                                     row.value.is_Skipped() ? "skipped"_str : "failed"_str,
                                     reason)
                            .as_str());
    }
    return Ok(rstd::move(output));
}
auto console::summary(slice<SummaryRow> rows) -> Result<String, String> {
    SummaryRenderer renderer;
    for (const auto& row : rows) renderer.include(row.name.as_str(), row.unit.as_str());
    auto output = renderer.header();
    for (const auto& row : rows) {
        auto text = renderer.render(row);
        if (text.is_err()) return Err(rstd::move(text).unwrap_err());
        output.push_str(text->as_str());
    }
    return Ok(rstd::move(output));
}

auto scope_name(bench::MeasurementScope scope) -> ref<str> {
    if (scope == bench::MeasurementScope::External) return "external"_str;
    if (scope == bench::MeasurementScope::Batched) return "batched"_str;
    if (scope == bench::MeasurementScope::BatchedRef) return "batched-ref"_str;
    return "repeated"_str;
}

void timing_details(String&                 output,
                    const BenchmarkResult&  measured,
                    const BenchmarkSummary& summary,
                    bool                    verbose) {
    const auto& work = measured.run_config();
    if (verbose) {
        output.push_str("  epoch        elapsed [ns]       iterations\n"_str);
        usize index;
        for (const auto& epoch : measured.measurements()) {
            column(output, rstd::format("{}", ++index).as_str(), usize(5));
            column(output, rstd::format("{}", epoch.elapsed.nanoseconds()).as_str(), usize(18));
            column(output, rstd::format("{}", epoch.iterations).as_str());
            output.push_ascii('\n');
        }
    }
    const auto& availability = measured.counter_availability();
    if (availability.is_Unavailable())
        output.push_str(
            rstd::format("  counters unavailable: {}\n", availability.as_Unavailable().code)
                .as_str());
    if (verbose) {
        output.push_str(
            rstd::format("  epochs: {}; operations: {}; warmup: {}; scope: {}\n  items/op: {}; "
                         "bytes/op: {}; units/op: {}; unit: {}\n  collector: {}\n",
                         measured.measurements().len(),
                         summary.total_iterations,
                         measured.config().warmup_iterations,
                         scope_name(measured.scope()),
                         work.items_per_iteration,
                         work.bytes_per_iteration,
                         work.batch,
                         console::escaped(work.unit.as_str()),
                         console::escaped(measured.collector_identity()))
                .as_str());
        if (measured.batch_size().is_some())
            output.push_str(
                rstd::format("  batch max items: {}\n", *measured.batch_size()).as_str());
        output.push_str(
            rstd::format("  median: {} ns/op; mdape: {}%\n",
                         summary.median_ns_per_unit * work.batch,
                         console::number(summary.median_absolute_percentage_error * f64(100)))
                .as_str());
        auto metric = [&](ref<str> name, Option<f64> value) {
            output.push_str(
                rstd::format("  {}: {}\n",
                             name,
                             value.is_some() ? console::number(*value) : "unavailable"_Str)
                    .as_str());
        };
        if (! availability.is_Disabled()) {
            metric("instructions/unit"_str, summary.instructions_per_unit);
            metric("cycles/unit"_str, summary.cycles_per_unit);
            metric("instructions/cycle"_str, summary.instructions_per_cycle);
            metric("branches/unit"_str, summary.branches_per_unit);
            metric("branch miss ratio"_str, summary.branch_miss_ratio);
        }
    }
}

void diagnostic_details(String&                      output,
                        const DiagnosticMeasurement& measured,
                        const AllocationSummary&     summary,
                        bool                         verbose) {
    if (summary.allocations.is_Unavailable())
        output.push_str(
            rstd::format("  allocations {}\n", console::exact(summary.allocations)).as_str());
    if (verbose) {
        output.push_str(
            rstd::format("  operations: {}; scope: {}; items/op: {}; bytes/op: {}\n  "
                         "allocations: {}; deallocations: {}; grows: {}; shrinks: {}; "
                         "failures: {}\n  requested bytes: {}; allocs/op: {}; allocated "
                         "bytes/op: {}\n  live bytes: {} -> {}; peak: {}; cleanup: {}\n",
                         measured.iterations,
                         scope_name(measured.scope),
                         measured.work.items_per_iteration,
                         measured.work.bytes_per_iteration,
                         console::exact(summary.allocations),
                         console::exact(summary.deallocations),
                         console::exact(summary.grows),
                         console::exact(summary.shrinks),
                         console::exact(summary.failures),
                         console::exact(summary.requested_bytes),
                         console::exact(summary.allocations_per_op),
                         console::exact(summary.bytes_per_op),
                         console::exact(summary.before_live),
                         console::exact(summary.after_live),
                         console::exact(summary.peak_live),
                         console::exact(summary.cleanup_live))
                .as_str());
        if (measured.batch_size.is_some())
            output.push_str(rstd::format("  batch max items: {}\n", *measured.batch_size).as_str());
    }
    for (const auto& metric : measured.metrics) {
        output.push_str(rstd::format("  {}: {}",
                                     console::escaped(metric.name.as_str()),
                                     diagnostic_value(metric.value, metric.unit))
                            .as_str());
        if (verbose)
            output.push_str(rstd::format(" [{}; {}; raw={}]",
                                         console::escaped(metric.scope.as_str()),
                                         console::escaped(metric.source.as_str()),
                                         console::exact(metric.value))
                                .as_str());
        else if (metric.value.is_Unavailable())
            output.push_str(
                rstd::format(": {}",
                             console::escaped(metric.value.as_Unavailable().reason.as_str()))
                    .as_str());
        output.push_ascii('\n');
    }
}

void ConsoleRenderer::include(ref<str> name) {
    auto display = console::escaped(name);
    if (console_ascii(display.as_str()))
        name_width_ = name_width_.max(display.len().min(usize(48)));
}

auto ConsoleRenderer::render_table(const bench::RunResult& result) -> String {
    const auto& table      = *fixed_table_;
    const auto& name       = options_.use_case_names && ! result.descriptor.name.is_empty()
                                 ? result.descriptor.name
                                 : result.descriptor.id;
    auto        rate_width = [](const ConsoleRateColumn& rate) {
        return console::escaped(rate.label.as_str()).len().max(usize(15));
    };
    String output;
    if (! fixed_started_) {
        if (! table.title.is_empty())
            output.push_str(
                rstd::format("\n{}\n", console::escaped(table.title.as_str())).as_str());
        cell(output, "case"_str, name_width_, true);
        if (options_.repeats > u64(1)) column(output, "repeat"_str, usize(8));
        column(output, "time/op"_str);
        usize width = name_width_ + usize(17 + 9 + 20 + 10);
        if (options_.repeats > u64(1)) width += usize(10);
        for (const auto& rate : table.rates) {
            column(output, console::escaped(rate.label.as_str()).as_str(), rate_width(rate));
            width += rate_width(rate) + usize(2);
        }
        column(output, "samples"_str, usize(7));
        column(output, "source"_str, usize(18));
        column(output, "status"_str, usize(8));
        output.push_ascii('\n');
        divider(output, width);
        fixed_started_ = true;
    }
    name_cell(output, name.as_str(), name_width_);
    if (options_.repeats > u64(1))
        column(output,
               rstd::format("{}/{}", result.repetition + u64(1), options_.repeats).as_str(),
               usize(8));
    if (result.outcome.is_Passed()) {
        const auto& measured = result.outcome.as_Passed().measurement;
        auto        summary  = measured.summary();
        column(output,
               console::time(summary.selected_ns_per_unit * measured.run_config().batch).as_str());
        for (const auto& wanted : table.rates) {
            String value = "-"_Str;
            for (const auto& rate : summary.rates) {
                if (rate.name != wanted.name || rate.unit != wanted.unit) continue;
                value = rate.unit == WorkUnit::Bytes ? console::byte_rate(rate.per_second)
                                                     : console::item_rate(rate.per_second);
                break;
            }
            column(output, value.as_str(), rate_width(wanted));
        }
        column(output, rstd::format("{}", measured.measurements().len()).as_str(), usize(7));
        column(output, console::escaped(measured.timing().source.as_str()).as_str(), usize(18));
        column(output, "ok"_str, usize(8));
        output.push_ascii('\n');
        if (options_.verbose) {
            const auto& timing = measured.timing();
            output.push_str(rstd::format("  source: {}; boundary: {}; resolution: {}\n",
                                         console::escaped(timing.source.as_str()),
                                         console::escaped(timing.boundary.as_str()),
                                         timing.resolution_ns.is_some()
                                             ? console::time(*timing.resolution_ns)
                                             : "unknown"_Str)
                                .as_str());
            timing_details(output, measured, summary, true);
        }
    } else {
        column(output, "-"_str);
        for (const auto& rate : table.rates) column(output, "-"_str, rate_width(rate));
        column(output, "-"_str, usize(7));
        column(output, "-"_str, usize(18));
        column(output,
               result.outcome.is_Skipped()  ? "skipped"_str
               : result.outcome.is_Failed() ? "failed"_str
                                            : "untimed"_str,
               usize(8));
        output.push_ascii('\n');
        if (result.outcome.is_Skipped())
            output.push_str(
                rstd::format("  {}\n",
                             console::escaped(result.outcome.as_Skipped().reason.as_str()))
                    .as_str());
        if (result.outcome.is_Failed())
            for (const auto& error : result.outcome.as_Failed().errors)
                output.push_str(rstd::format("  {}\n", console::escaped(error.as_str())).as_str());
    }
    if (options_.verbose && result.outcome.is_Diagnosed()) {
        const auto& measured = result.outcome.as_Diagnosed().measurement;
        auto        summary  = measured.allocation_summary();
        diagnostic_details(output, measured, summary, true);
    }
    return output;
}

auto ConsoleRenderer::render(const bench::RunResult& result) -> String {
    if (fixed_table_) return render_table(result);
    const auto& name = options_.use_case_names && ! result.descriptor.name.is_empty()
                           ? result.descriptor.name
                           : result.descriptor.id;
    String      output;
    if (result.outcome.is_Failed() || result.outcome.is_Skipped()) {
        name_cell(output, name.as_str(), name_width_);
        if (options_.repeats > u64(1))
            column(output,
                   rstd::format("{}/{}", result.repetition + u64(1), options_.repeats).as_str(),
                   usize(8));
        output.push_str(result.outcome.is_Failed() ? "  failed\n"_str : "  skipped\n"_str);
        if (result.outcome.is_Skipped())
            output.push_str(
                rstd::format("  {}\n",
                             console::escaped(result.outcome.as_Skipped().reason.as_str()))
                    .as_str());
        else
            for (const auto& error : result.outcome.as_Failed().errors)
                output.push_str(rstd::format("  {}\n", console::escaped(error.as_str())).as_str());
        return output;
    }
    bool timing = result.outcome.is_Passed();
    bool header = table_ != (timing ? 1 : 2);
    if (header) {
        items_ = false;
        bytes_ = false;
        rate_columns_.clear();
    }
    if (timing) {
        const auto& measured = result.outcome.as_Passed().measurement;
        const auto& work     = measured.run_config();
        if (statistic_ != work.statistic) {
            statistic_ = work.statistic;
            header     = true;
        }
        auto source     = console::escaped(measured.timing().source.as_str());
        auto boundary   = console::escaped(measured.timing().boundary.as_str());
        auto work_label = rstd::format("{} {}", work.batch, console::escaped(work.unit.as_str()));
        auto resolution = measured.timing().resolution_ns.is_some()
                              ? console::time(*measured.timing().resolution_ns)
                              : "unknown"_Str;
        if (table_ != 1 || source != timing_source_ || boundary != timing_boundary_ ||
            work_label != work_label_ || resolution != resolution_label_) {
            const auto source_width     = source.len().max(usize(6));
            const auto boundary_width   = boundary.len().max(usize(8));
            const auto work_width       = work_label.len().max(usize(7));
            const auto resolution_width = resolution.len().max(usize(10));
            cell(output, "source"_str, source_width, true);
            column(output, "boundary"_str, boundary_width);
            column(output, "resolution"_str, resolution_width);
            column(output, "work/op"_str, work_width);
            output.push_ascii('\n');
            divider(output,
                    source_width + boundary_width + resolution_width + work_width + usize(6));
            cell(output, source.as_str(), source_width, true);
            column(output, boundary.as_str(), boundary_width);
            column(output, resolution.as_str(), resolution_width);
            column(output, work_label.as_str(), work_width);
            output.push_str("\n\n"_str);
            timing_source_    = rstd::move(source);
            timing_boundary_  = rstd::move(boundary);
            work_label_       = rstd::move(work_label);
            resolution_label_ = rstd::move(resolution);
            header            = true;
        }
        for (const auto& quantity : measured.work()) {
            if (quantity.name == "items"_str || quantity.name == "bytes"_str) continue;
            bool found = false;
            for (const auto& existing : rate_columns_)
                if (existing.name == quantity.name && existing.unit == quantity.unit) found = true;
            if (found) continue;
            auto label = rstd::format("{} [{}]",
                                      console::escaped(quantity.name.as_str()),
                                      quantity.unit == WorkUnit::Flops   ? "FLOP/s"_str
                                      : quantity.unit == WorkUnit::Bytes ? "B/s"_str
                                                                         : "count/s"_str);
            auto width = label.len().max(usize(15));
            rate_columns_.push({ quantity.name.clone(), quantity.unit, rstd::move(label), width });
            header = true;
        }
        if (work.items_per_iteration != u64() && ! items_) {
            items_ = true;
            header = true;
        }
        if (work.bytes_per_iteration != u64() && ! bytes_) {
            bytes_ = true;
            header = true;
        }
    }
    table_ = timing ? 1 : 2;
    if (header) {
        cell(output, "case"_str, name_width_, true);
        if (options_.repeats > u64(1)) column(output, "repeat"_str, usize(8));
        if (timing) {
            column(output, statistic_ == SummaryStatistic::Best ? "best/op"_str : "median/op"_str);
            if (items_) column(output, "items/s"_str);
            if (bytes_) column(output, "bytes/s"_str);
            for (const auto& rate : rate_columns_) column(output, rate.label.as_str(), rate.width);
            column(output, "samples"_str, usize(7));
        } else {
            column(output, "allocs/op"_str);
            column(output, "allocated/op"_str);
            column(output, "peak live"_str);
            column(output, "cleanup live"_str);
        }
        output.push_ascii('\n');
        auto width = name_width_ + (options_.repeats > u64(1) ? usize(10) : usize()) +
                     usize(17) * (timing ? usize(1 + int(items_) + int(bytes_)) : usize(4));
        if (timing) {
            width += usize(9);
            for (const auto& rate : rate_columns_) width += rate.width + usize(2);
        }
        divider(output, width);
    }
    name_cell(output, name.as_str(), name_width_);
    if (options_.repeats > u64(1))
        column(output,
               rstd::format("{}/{}", result.repetition + u64(1), options_.repeats).as_str(),
               usize(8));
    if (timing) {
        const auto& measured = result.outcome.as_Passed().measurement;
        auto        summary  = measured.summary();
        const auto& work     = measured.run_config();
        column(output, console::time(summary.selected_ns_per_unit * work.batch).as_str());
        if (items_)
            column(output,
                   work.items_per_iteration == u64() ? "-"_Str
                   : summary.rate("items"_str).is_some()
                       ? console::item_rate(*summary.rate("items"_str))
                       : "unavailable"_Str);
        if (bytes_)
            column(output,
                   work.bytes_per_iteration == u64() ? "-"_Str
                   : summary.rate("bytes"_str).is_some()
                       ? console::byte_rate(*summary.rate("bytes"_str))
                       : "unavailable"_Str);
        for (const auto& rate_column : rate_columns_) {
            String value = "-"_Str;
            for (const auto& rate : summary.rates) {
                if (rate.name != rate_column.name || rate.unit != rate_column.unit) continue;
                value = rate.unit == WorkUnit::Bytes ? console::byte_rate(rate.per_second)
                                                     : console::item_rate(rate.per_second);
                break;
            }
            column(output, value.as_str(), rate_column.width);
        }
        column(output, rstd::format("{}", measured.measurements().len()).as_str(), usize(7));
        output.push_ascii('\n');
        timing_details(output, measured, summary, options_.verbose);
    } else {
        const auto& measured = result.outcome.as_Diagnosed().measurement;
        auto        summary  = measured.allocation_summary();
        column(output,
               diagnostic_value(summary.allocations_per_op, bench::DiagnosticUnit::Count).as_str());
        column(output,
               diagnostic_value(summary.bytes_per_op, bench::DiagnosticUnit::Bytes).as_str());
        column(output, diagnostic_value(summary.peak_live, bench::DiagnosticUnit::Bytes).as_str());
        column(output,
               diagnostic_value(summary.cleanup_live, bench::DiagnosticUnit::Bytes).as_str());
        output.push_ascii('\n');
        diagnostic_details(output, measured, summary, options_.verbose);
    }
    return output;
}

auto console::context(bool                         quick,
                      bool                         diagnostic,
                      u64                          repeats,
                      bool                         verbose,
                      const bench::RunEnvironment& environment) -> String {
    String output =
        rstd::format("mode: {}\n",
                     diagnostic ? "diagnostic (no timing; tracked allocation requests)"_str
                     : quick    ? "quick (smoke only)"_str
                                : "timing"_str);
    bool profile = false;
    for (const auto& field : environment.fields) {
        if (field.name == "profile"_str) profile = true;
        if (verbose || field.name == "profile"_str || field.name == "compiler"_str) {
            output.push_str(
                rstd::format("{}: {}",
                             escaped(field.name.as_str()),
                             field.value.is_some() ? escaped(field.value->as_str()) : "unknown"_Str)
                    .as_str());
            if (verbose)
                output.push_str(rstd::format(" [{}]", escaped(field.source.as_str())).as_str());
            output.push_ascii('\n');
        }
    }
    if (! profile) output.push_str("profile: unknown\n"_str);
    if (repeats > u64(1))
        output.push_str(rstd::format("repeats: {} (same process)\n", repeats).as_str());
    output.push_ascii('\n');
    return output;
}

auto console::comparison(const bench::ComparisonReport& report, bool verbose) -> String {
    String output = report.mode == bench::ComparisonMode::Strict
                        ? "comparison: strict\n"_Str
                        : "comparison: cross-environment (descriptive only)\n"_Str;
    if (! report.environment_differences.is_empty()) {
        output.push_str("environment missing or different: "_str);
        bool first = true;
        for (const auto& field : report.environment_differences) {
            if (! first) output.push_str(", "_str);
            first = false;
            output.push_str(escaped(field.as_str()).as_str());
        }
        output.push_ascii('\n');
    }
    usize width(4);
    for (const auto& value : report.cases) {
        auto name = escaped(value.id.as_str());
        if (console_ascii(name.as_str())) width = width.max(name.len().min(usize(48)));
    }
    output.push_ascii('\n');
    cell(output, "case"_str, width, true);
    column(output, "baseline"_str);
    column(output, "candidate"_str);
    column(output, "change"_str);
    output.push_ascii('\n');
    divider(output, width + usize(51));
    for (const auto& value : report.cases) {
        name_cell(output, value.id.as_str(), width);
        if (value.candidate_over_baseline.is_none()) {
            output.push_str("  not comparable\n"_str);
            for (const auto& reason : value.reasons)
                output.push_str(rstd::format("  {}\n", escaped(reason.as_str())).as_str());
            continue;
        }
        auto           before = *value.baseline_ns_per_op;
        auto           after  = *value.candidate_ns_per_op;
        const ref<str> units[] { "ns"_str, "us"_str, "ms"_str, "s"_str };
        usize          index;
        while (index < usize(3) && before.abs().max(after.abs()) >= f64(999.95)) {
            before /= f64(1000);
            after /= f64(1000);
            ++index;
        }
        column(output, rstd::format("{} {}", number(before), units[index.to_primitive()]).as_str());
        column(output, rstd::format("{} {}", number(after), units[index.to_primitive()]).as_str());
        auto change = (*value.candidate_over_baseline - f64(1)) * f64(100);
        column(output,
               rstd::format("{}{}%", change > f64() ? "+"_str : ""_str, number(change)).as_str());
        output.push_ascii('\n');
        if (verbose)
            output.push_str(
                rstd::format("  baseline: {} ns/op; candidate: {} ns/op; candidate/baseline: {}\n",
                             *value.baseline_ns_per_op,
                             *value.candidate_ns_per_op,
                             *value.candidate_over_baseline)
                    .as_str());
    }
    return output;
}
auto console::write(ref<str> text) -> Result<empty, String> {
    auto output  = rstd::io::stdout();
    auto written = rstd::io::write_all(output, text.as_bytes());
    if (written.is_err())
        return Err(rstd::format("cannot write benchmark output: {}", written.unwrap_err()));
    return Ok(empty {});
}
auto console::write_comparison(const bench::ComparisonReport& report, bool verbose)
    -> Result<bool, String> {
    auto rendered = comparison(report, verbose);
    rstd_try(write(rendered.as_str()));
    bool comparable = ! report.cases.is_empty();
    for (const auto& value : report.cases)
        if (value.candidate_over_baseline.is_none()) comparable = false;
    return Ok(comparable);
}
