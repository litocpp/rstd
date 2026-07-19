export module rstd.bench:probe.report;
export import :probe.collector;

using namespace rstd::prelude;

template<typename Writer>
    requires Impled<Writer, rstd::io::Write>
auto write_probe_text_line(Writer& writer, String line) -> rstd::io::Result<empty> {
    return rstd::io::write_all(writer, rstd::str_::as_bytes(line.as_str()));
}

auto format_probe_diagnostic(const rstd::bench::probe::ProbeDiagnostic& diagnostic) -> String {
    if (diagnostic.is_InvalidProbe()) {
        return rstd::format("diagnostic=invalid_probe value={}\n",
                            diagnostic.as_InvalidProbe().value);
    }
    if (diagnostic.is_WrongThread()) {
        return rstd::format("diagnostic=wrong_thread expected={} actual={}\n",
                            diagnostic.as_WrongThread().expected,
                            diagnostic.as_WrongThread().actual);
    }
    if (diagnostic.is_ActiveSpanOverflow()) {
        return rstd::format("diagnostic=active_span_overflow capacity={}\n",
                            diagnostic.as_ActiveSpanOverflow().capacity);
    }
    if (diagnostic.is_NonLifo()) {
        return rstd::format("diagnostic=non_lifo expected={} actual={}\n",
                            diagnostic.as_NonLifo().expected,
                            diagnostic.as_NonLifo().actual);
    }
    if (diagnostic.is_ActiveSpansPending()) {
        return rstd::format("diagnostic=active_spans_pending count={}\n",
                            diagnostic.as_ActiveSpansPending().count);
    }
    if (diagnostic.is_FrameAlreadyActive()) {
        return rstd::format("diagnostic=frame_already_active\n");
    }
    if (diagnostic.is_NoActiveFrame()) {
        return rstd::format("diagnostic=no_active_frame\n");
    }
    if (diagnostic.is_FrameStillActive()) {
        return rstd::format("diagnostic=frame_still_active\n");
    }
    if (diagnostic.is_SequenceExhausted()) {
        return rstd::format("diagnostic=sequence_exhausted\n");
    }
    return rstd::format("diagnostic=clock_stalled\n");
}

export namespace rstd::bench::probe
{

template<typename Writer>
    requires Impled<Writer, io::Write>
auto write_text(Writer& writer, const ProbeReport& report) -> io::Result<empty> {
    for (usize index; index < report.overall().len(); ++index) {
        const auto& summary = report.overall()[index];
        auto        label   = report.schema()->label(summary.probe).unwrap();
        auto        result =
            write_probe_text_line(writer,
                                  rstd::format("probe={} count={} inclusive_ns={} exclusive_ns={} "
                                               "mean_ns={} median_ns={} p95_ns={}\n",
                                               label,
                                               summary.count,
                                               summary.inclusive_total.as_nanos(),
                                               summary.exclusive_total.as_nanos(),
                                               summary.mean_ns,
                                               summary.median_ns,
                                               summary.p95_ns));
        if (result.is_err()) return result;
    }

    for (usize index; index < report.by_thread().len(); ++index) {
        const auto& value  = report.by_thread()[index];
        auto        label  = report.schema()->label(value.summary.probe).unwrap();
        auto        result = write_probe_text_line(
            writer,
            rstd::format("thread={} probe={} count={} inclusive_ns={} exclusive_ns={}\n",
                         value.thread_id.as_u64().get(),
                         label,
                         value.summary.count,
                         value.summary.inclusive_total.as_nanos(),
                         value.summary.exclusive_total.as_nanos()));
        if (result.is_err()) return result;
    }

    for (usize index; index < report.by_frame().len(); ++index) {
        const auto& value  = report.by_frame()[index];
        auto        label  = report.schema()->label(value.summary.probe).unwrap();
        auto        result = write_probe_text_line(
            writer,
            rstd::format("frame={} probe={} count={} inclusive_ns={} exclusive_ns={}\n",
                         value.frame,
                         label,
                         value.summary.count,
                         value.summary.inclusive_total.as_nanos(),
                         value.summary.exclusive_total.as_nanos()));
        if (result.is_err()) return result;
    }

    auto dropped = write_probe_text_line(
        writer, rstd::format("dropped_samples={}\n", report.dropped_samples()));
    if (dropped.is_err()) return dropped;

    for (usize index; index < report.diagnostics().len(); ++index) {
        const auto& diagnostic = report.diagnostics()[index];
        auto        result     = write_probe_text_line(writer, format_probe_diagnostic(diagnostic));
        if (result.is_err()) return result;
    }
    return Ok(empty {});
}

} // namespace rstd::bench::probe
