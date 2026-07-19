module;
#include <rstd/enum.hpp>

export module rstd.bench:probe.model;
export import :clock;

using namespace rstd::prelude;
using rstd::sync::Arc;

#define RSTD_BENCH_OVERFLOW_POLICY_VARIANTS(V) \
    V(DropNewest)                              \
    V(Grow)

#define RSTD_BENCH_THREAD_POLICY_VARIANTS(V) \
    V(Verify)                                \
    V(Unchecked)

#define RSTD_BENCH_PROBE_DIAGNOSTIC_VARIANTS(V) \
    V(InvalidProbe, (u32 value;))               \
    V(WrongThread, (u64 expected; u64 actual;)) \
    V(ActiveSpanOverflow, (usize capacity;))    \
    V(NonLifo, (u64 expected; u64 actual;))     \
    V(ActiveSpansPending, (usize count;))       \
    V(FrameAlreadyActive, ())                   \
    V(NoActiveFrame, ())                        \
    V(FrameStillActive, ())                     \
    V(SequenceExhausted, ())                    \
    V(ClockStalled, ())

#define RSTD_BENCH_PROBE_ERROR_VARIANTS(V)   \
    V(Diagnostic, (ProbeDiagnostic reason;)) \
    V(ProbeIdExhausted, ())                  \
    V(SchemaMismatch, ())

export namespace rstd::bench::probe
{

RSTD_TAG_ENUM(OverflowPolicy, RSTD_BENCH_OVERFLOW_POLICY_VARIANTS)
RSTD_TAG_ENUM(ThreadPolicy, RSTD_BENCH_THREAD_POLICY_VARIANTS)
RSTD_ENUM(ProbeDiagnostic, RSTD_BENCH_PROBE_DIAGNOSTIC_VARIANTS)
RSTD_ENUM(ProbeError, RSTD_BENCH_PROBE_ERROR_VARIANTS)

struct ProbeId {
    u32 value;

    static constexpr auto from_u32(u32 value) noexcept -> ProbeId { return { value }; }
    constexpr auto        index() const noexcept -> usize {
        return usize(static_cast<rstd::size_t>(value.to_primitive()));
    }

    friend constexpr bool operator==(ProbeId, ProbeId) noexcept = default;
};

struct RecorderConfig {
    usize          sample_capacity { usize(4096) };
    usize          active_span_capacity { usize(64) };
    OverflowPolicy overflow { OverflowPolicy::DropNewest() };
    ThreadPolicy   thread_policy { ThreadPolicy::Verify() };
};

struct SpanSample {
    ProbeId          probe;
    u64              started_ns;
    time::Duration   elapsed;
    u64              recorder_id;
    thread::ThreadId thread_id;
    Option<u64>      frame;
    u64              sequence;
    Option<u64>      parent_sequence;
};

class ProbeSchema {
    Vec<String> labels_;

public:
    explicit ProbeSchema(Vec<String> labels): labels_(rstd::move(labels)) {}

    auto len() const noexcept -> usize { return labels_.len(); }
    auto is_empty() const noexcept -> bool { return labels_.is_empty(); }
    auto contains(ProbeId id) const noexcept -> bool { return id.index() < labels_.len(); }
    auto label(ProbeId id) const noexcept -> Option<ref<str>> {
        if (! contains(id)) return None();
        return Some(labels_[id.index()].as_str());
    }
};

class ProbeBatch {
    Arc<ProbeSchema> schema_;
    Vec<SpanSample>  samples_;
    u64              recorder_id_;
    thread::ThreadId thread_id_;
    usize            dropped_samples_;

public:
    ProbeBatch(Arc<ProbeSchema> schema,
               Vec<SpanSample>  samples,
               u64              recorder_id,
               thread::ThreadId thread_id,
               usize            dropped_samples)
        : schema_(rstd::move(schema)),
          samples_(rstd::move(samples)),
          recorder_id_(recorder_id),
          thread_id_(thread_id),
          dropped_samples_(dropped_samples) {}

    ProbeBatch(const ProbeBatch&)                        = delete;
    auto operator=(const ProbeBatch&) -> ProbeBatch&     = delete;
    ProbeBatch(ProbeBatch&&) noexcept                    = default;
    auto operator=(ProbeBatch&&) noexcept -> ProbeBatch& = default;

    auto schema() const noexcept -> ref<ProbeSchema> { return schema_.deref(); }
    auto schema_owner() const noexcept -> Arc<ProbeSchema> { return schema_.clone(); }
    auto samples() const noexcept -> slice<SpanSample> { return samples_.as_slice(); }
    auto recorder_id() const noexcept -> u64 { return recorder_id_; }
    auto thread_id() const noexcept -> thread::ThreadId { return thread_id_; }
    auto dropped_samples() const noexcept -> usize { return dropped_samples_; }

    auto take_samples() noexcept -> Vec<SpanSample> { return rstd::move(samples_); }
};

} // namespace rstd::bench::probe

#undef RSTD_BENCH_OVERFLOW_POLICY_VARIANTS
#undef RSTD_BENCH_THREAD_POLICY_VARIANTS
#undef RSTD_BENCH_PROBE_DIAGNOSTIC_VARIANTS
#undef RSTD_BENCH_PROBE_ERROR_VARIANTS
