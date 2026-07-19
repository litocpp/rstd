export module rstd.bench:probe.collector;
export import :probe.recorder;
import :statistics;

using namespace rstd::prelude;
using rstd::sync::Arc;

auto probe_elapsed_ns(rstd::time::Duration elapsed) noexcept -> u64 {
    auto const nanos = elapsed.as_nanos();
    if (nanos > u128(u64::MAX.to_primitive())) return u64::MAX;
    return u64(nanos.to_primitive());
}

auto probe_saturating_add(u64 left, u64 right) noexcept -> u64 {
    auto value = left.checked_add(right);
    return value.is_some() ? rstd::move(value).unwrap_unchecked() : u64::MAX;
}

struct ProbeSummaryBuilder {
    rstd::bench::probe::ProbeId probe;
    usize                       count {};
    u64                         inclusive_total {};
    u64                         exclusive_total {};
    u64                         minimum { u64::MAX };
    u64                         maximum {};
    Vec<f64>                    inclusive_values;

    explicit ProbeSummaryBuilder(rstd::bench::probe::ProbeId probe)
        : probe(probe), inclusive_values(Vec<f64>::make()) {}

    void push(u64 inclusive, u64 exclusive) {
        ++count;
        inclusive_total = probe_saturating_add(inclusive_total, inclusive);
        exclusive_total = probe_saturating_add(exclusive_total, exclusive);
        if (inclusive < minimum) minimum = inclusive;
        if (inclusive > maximum) maximum = inclusive;
        inclusive_values.push(f64(static_cast<double>(inclusive.to_primitive())));
    }
};

export namespace rstd::bench::probe
{

struct ProbeSummary {
    ProbeId        probe;
    usize          count;
    time::Duration inclusive_total;
    time::Duration exclusive_total;
    f64            minimum_ns;
    f64            maximum_ns;
    f64            mean_ns;
    f64            median_ns;
    f64            p95_ns;
    f64            exclusive_mean_ns;
};

struct ThreadProbeSummary {
    thread::ThreadId thread_id;
    ProbeSummary     summary;
};

struct FrameProbeSummary {
    u64          frame;
    ProbeSummary summary;
};

class ProbeReport {
    Arc<ProbeSchema>        schema_;
    Vec<ProbeSummary>       overall_;
    Vec<ThreadProbeSummary> threads_;
    Vec<FrameProbeSummary>  frames_;
    usize                   dropped_samples_;
    Vec<ProbeDiagnostic>    diagnostics_;

public:
    ProbeReport(Arc<ProbeSchema>        schema,
                Vec<ProbeSummary>       overall,
                Vec<ThreadProbeSummary> threads,
                Vec<FrameProbeSummary>  frames,
                usize                   dropped_samples,
                Vec<ProbeDiagnostic>    diagnostics)
        : schema_(rstd::move(schema)),
          overall_(rstd::move(overall)),
          threads_(rstd::move(threads)),
          frames_(rstd::move(frames)),
          dropped_samples_(dropped_samples),
          diagnostics_(rstd::move(diagnostics)) {}

    auto schema() const noexcept -> ref<ProbeSchema> { return schema_.deref(); }
    auto overall() const noexcept -> slice<ProbeSummary> { return overall_.as_slice(); }
    auto by_thread() const noexcept -> slice<ThreadProbeSummary> { return threads_.as_slice(); }
    auto by_frame() const noexcept -> slice<FrameProbeSummary> { return frames_.as_slice(); }
    auto dropped_samples() const noexcept -> usize { return dropped_samples_; }
    auto diagnostics() const noexcept -> slice<ProbeDiagnostic> { return diagnostics_.as_slice(); }
};

class ProbeCollector {
    Arc<ProbeSchema>     schema_;
    Vec<SpanSample>      samples_;
    usize                dropped_samples_ {};
    Vec<ProbeDiagnostic> diagnostics_;

    auto exclusive_ns(const SpanSample& sample) const noexcept -> u64 {
        auto children = u64();
        for (const auto& candidate : samples_) {
            if (candidate.recorder_id == sample.recorder_id &&
                candidate.parent_sequence == Some(sample.sequence)) {
                children = probe_saturating_add(children, probe_elapsed_ns(candidate.elapsed));
            }
        }
        return probe_elapsed_ns(sample.elapsed).saturating_sub(children);
    }

    static auto builder(Vec<ProbeSummaryBuilder>& builders, ProbeId id) -> ProbeSummaryBuilder& {
        for (auto& current : builders) {
            if (current.probe == id) return current;
        }
        return builders.emplace_back(id);
    }

    static auto finish_builder(ProbeSummaryBuilder&& builder) -> ProbeSummary {
        auto median_values = Vec<f64>::with_capacity(builder.inclusive_values.len());
        auto p95_values    = Vec<f64>::with_capacity(builder.inclusive_values.len());
        for (auto value : builder.inclusive_values) {
            median_values.push(f64(value.to_primitive()));
            p95_values.push(f64(value.to_primitive()));
        }
        auto const count = static_cast<double>(builder.count.to_primitive());
        return ProbeSummary {
            .probe           = builder.probe,
            .count           = builder.count,
            .inclusive_total = time::Duration::from_nanos(builder.inclusive_total),
            .exclusive_total = time::Duration::from_nanos(builder.exclusive_total),
            .minimum_ns      = builder.count == usize()
                                   ? f64()
                                   : f64(static_cast<double>(builder.minimum.to_primitive())),
            .maximum_ns      = f64(static_cast<double>(builder.maximum.to_primitive())),
            .mean_ns =
                builder.count == usize()
                    ? f64()
                    : f64(static_cast<double>(builder.inclusive_total.to_primitive()) / count),
            .median_ns = bench::median(rstd::move(median_values)),
            .p95_ns    = bench::percentile(rstd::move(p95_values), f64(0.95)),
            .exclusive_mean_ns =
                builder.count == usize()
                    ? f64()
                    : f64(static_cast<double>(builder.exclusive_total.to_primitive()) / count),
        };
    }

public:
    explicit ProbeCollector(Arc<ProbeSchema> schema)
        : schema_(rstd::move(schema)),
          samples_(Vec<SpanSample>::make()),
          diagnostics_(Vec<ProbeDiagnostic>::make()) {}

    static auto new_(Arc<ProbeSchema> schema) -> ProbeCollector {
        return ProbeCollector(rstd::move(schema));
    }

    auto ingest(const ProbeBatch& batch) -> Result<empty, ProbeError> {
        auto batch_schema = batch.schema_owner();
        if (! Arc<ProbeSchema>::ptr_eq(schema_, batch_schema)) {
            return Err(ProbeError::SchemaMismatch());
        }
        samples_.reserve(batch.samples().len());
        for (usize index; index < batch.samples().len(); ++index) {
            samples_.push(SpanSample(batch.samples()[index]));
        }
        auto dropped     = dropped_samples_.checked_add(batch.dropped_samples());
        dropped_samples_ = dropped.is_some() ? rstd::move(dropped).unwrap_unchecked() : usize::MAX;
        return Ok(empty {});
    }

    void record_diagnostic(ProbeDiagnostic diagnostic) {
        diagnostics_.push(rstd::move(diagnostic));
    }

    auto finish() && -> ProbeReport {
        auto overall_builders = Vec<ProbeSummaryBuilder>::make();
        struct ThreadBuilder {
            thread::ThreadId    thread_id;
            ProbeSummaryBuilder builder;
        };
        struct FrameBuilder {
            u64                 frame;
            ProbeSummaryBuilder builder;
        };
        auto thread_builders = Vec<ThreadBuilder>::make();
        auto frame_builders  = Vec<FrameBuilder>::make();

        for (const auto& sample : samples_) {
            auto const inclusive = probe_elapsed_ns(sample.elapsed);
            auto const exclusive = exclusive_ns(sample);
            builder(overall_builders, sample.probe).push(inclusive, exclusive);

            ThreadBuilder* thread_builder = nullptr;
            for (auto& current : thread_builders) {
                if (current.thread_id == sample.thread_id &&
                    current.builder.probe == sample.probe) {
                    thread_builder = &current;
                    break;
                }
            }
            if (thread_builder == nullptr) {
                thread_builder = &thread_builders.emplace_back(
                    ThreadBuilder { sample.thread_id, ProbeSummaryBuilder(sample.probe) });
            }
            thread_builder->builder.push(inclusive, exclusive);

            if (sample.frame.is_some()) {
                FrameBuilder* frame_builder = nullptr;
                for (auto& current : frame_builders) {
                    if (current.frame == *sample.frame && current.builder.probe == sample.probe) {
                        frame_builder = &current;
                        break;
                    }
                }
                if (frame_builder == nullptr) {
                    frame_builder = &frame_builders.emplace_back(
                        FrameBuilder { *sample.frame, ProbeSummaryBuilder(sample.probe) });
                }
                frame_builder->builder.push(inclusive, exclusive);
            }
        }

        auto overall = Vec<ProbeSummary>::with_capacity(overall_builders.len());
        for (auto& value : overall_builders) overall.push(finish_builder(rstd::move(value)));
        auto threads = Vec<ThreadProbeSummary>::with_capacity(thread_builders.len());
        for (auto& value : thread_builders) {
            threads.push(ThreadProbeSummary {
                value.thread_id,
                finish_builder(rstd::move(value.builder)),
            });
        }
        auto frames = Vec<FrameProbeSummary>::with_capacity(frame_builders.len());
        for (auto& value : frame_builders) {
            frames.push(FrameProbeSummary {
                value.frame,
                finish_builder(rstd::move(value.builder)),
            });
        }

        return ProbeReport(schema_.clone(),
                           rstd::move(overall),
                           rstd::move(threads),
                           rstd::move(frames),
                           dropped_samples_,
                           rstd::move(diagnostics_));
    }
};

} // namespace rstd::bench::probe
