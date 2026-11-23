export module rstd.bench:probe.collector;
export import :probe.recorder;
import :statistics;

using namespace rstd::prelude;
using rstd::collections::HashMap;
using rstd::sync::Arc;

struct ProbeScopedId {
    u64 scope;
    u64 id;

    friend auto operator==(ProbeScopedId, ProbeScopedId) noexcept -> bool = default;
};

namespace rstd
{

template<>
struct Impl<hash::Hash, ProbeScopedId> : ImplBase<ProbeScopedId> {
    template<typename H>
        requires Impled<H, hash::Hasher>
    void hash(H& state) const noexcept {
        hash::write_value(state, this->self().scope);
        hash::write_value(state, this->self().id);
    }
};

} // namespace rstd

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

    template<typename Key, typename Builder, typename MakeBuilder>
    static auto
    group(HashMap<Key, usize>& indices, Vec<Builder>& builders, Key key, MakeBuilder make)
        -> Builder& {
        if (auto index = indices.get(key); index.is_some()) {
            return builders[**index];
        }
        indices.insert(key, builders.len());
        return builders.emplace_back(make());
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
        // Batches can arrive in any order, and span sequences are recorder-local.
        auto child_times = HashMap<ProbeScopedId, u64>::make();
        for (const auto& sample : samples_) {
            if (sample.parent_sequence.is_none()) continue;
            auto key     = ProbeScopedId { sample.recorder_id, *sample.parent_sequence };
            auto elapsed = probe_elapsed_ns(sample.elapsed);
            if (auto total = child_times.get_mut(key); total.is_some()) {
                **total = probe_saturating_add(**total, elapsed);
            } else {
                child_times.insert(key, elapsed);
            }
        }
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
        auto overall_indices = HashMap<u32, usize>::make();
        auto thread_indices  = HashMap<ProbeScopedId, usize>::make();
        auto frame_indices   = HashMap<ProbeScopedId, usize>::make();

        for (const auto& sample : samples_) {
            auto const inclusive = probe_elapsed_ns(sample.elapsed);
            auto const children  = child_times.get({ sample.recorder_id, sample.sequence });
            auto const exclusive =
                inclusive.saturating_sub(children.is_some() ? **children : u64 {});
            auto const probe        = u64(sample.probe.value.to_primitive());
            auto       make_summary = [&] {
                return ProbeSummaryBuilder(sample.probe);
            };
            group(overall_indices, overall_builders, sample.probe.value, make_summary)
                .push(inclusive, exclusive);

            auto thread_key = ProbeScopedId { sample.thread_id.as_u64().get(), probe };
            group(thread_indices, thread_builders, thread_key, [&] {
                return ThreadBuilder { sample.thread_id, make_summary() };
            }).builder.push(inclusive, exclusive);

            if (sample.frame.is_some()) {
                auto frame_key = ProbeScopedId { *sample.frame, probe };
                group(frame_indices, frame_builders, frame_key, [&] {
                    return FrameBuilder { *sample.frame, make_summary() };
                }).builder.push(inclusive, exclusive);
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
