export module rstd.bench:probe.recorder;
export import :probe.registry;

using namespace rstd::prelude;
using rstd::sync::Arc;
using rstd::sync::atomic::Atomic;
using rstd::sync::atomic::Ordering;

namespace rstd::bench::probe
{

template<typename Clock>
    requires Impled<Clock, MonotonicClock>
struct ProbeRecorderState;

struct ActiveSpan {
    ProbeId     probe;
    u64         started_ns;
    u64         sequence;
    Option<u64> parent_sequence;
};

struct SpanEndStatus {
    bool                    ended {};
    Option<ProbeDiagnostic> diagnostic;
};

template<typename Clock>
    requires Impled<Clock, MonotonicClock>
struct ProbeRecorderState {
    Arc<ProbeSchema>        schema;
    Arc<Clock>              clock;
    RecorderConfig          config;
    thread::ThreadId        owner_thread;
    u64                     recorder_id;
    Vec<ActiveSpan>         active;
    Vec<SpanSample>         samples;
    Option<Vec<SpanSample>> spare;
    Option<u64>             frame;
    u64                     next_sequence { u64(1) };
    usize                   dropped_samples {};
    Option<ProbeDiagnostic> diagnostic;

    ProbeRecorderState(Arc<ProbeSchema> schema,
                       Arc<Clock>       clock,
                       RecorderConfig   config,
                       thread::ThreadId owner_thread,
                       u64              recorder_id)
        : schema(rstd::move(schema)),
          clock(rstd::move(clock)),
          config(rstd::move(config)),
          owner_thread(owner_thread),
          recorder_id(recorder_id),
          active(Vec<ActiveSpan>::with_capacity(this->config.active_span_capacity)),
          samples(Vec<SpanSample>::with_capacity(this->config.sample_capacity)),
          spare(Some(Vec<SpanSample>::with_capacity(this->config.sample_capacity))) {}

    auto now_ns() const noexcept -> u64 { return as<MonotonicClock>(*clock.deref()).now_ns(); }

    void remember(const ProbeDiagnostic& value) {
        if (diagnostic.is_none()) diagnostic = Some(ProbeDiagnostic(value));
    }

    auto verify_thread() -> Option<ProbeDiagnostic> {
        if (config.thread_policy.is_Unchecked()) return None();
        auto const actual = thread::current_id();
        if (actual == owner_thread) return None();
        auto value =
            ProbeDiagnostic::WrongThread(owner_thread.as_u64().get(), actual.as_u64().get());
        remember(value);
        return Some(rstd::move(value));
    }

    auto begin_span(ProbeId probe) -> Option<u64> {
        if (verify_thread().is_some()) return None();
        if (! schema->contains(probe)) {
            auto value = ProbeDiagnostic::InvalidProbe(probe.value);
            remember(value);
            return None();
        }
        if (active.len() >= config.active_span_capacity) {
            auto value = ProbeDiagnostic::ActiveSpanOverflow(config.active_span_capacity);
            remember(value);
            return None();
        }
        if (next_sequence == u64::MAX) {
            auto value = ProbeDiagnostic::SequenceExhausted();
            remember(value);
            return None();
        }

        auto const sequence = next_sequence;
        ++next_sequence;
        auto parent = Option<u64> {};
        if (! active.is_empty()) {
            parent = Some(u64(active[active.len() - usize(1)].sequence.to_primitive()));
        }
        active.push(ActiveSpan {
            .probe           = probe,
            .started_ns      = now_ns(),
            .sequence        = sequence,
            .parent_sequence = parent,
        });
        return Some(u64(sequence.to_primitive()));
    }

    auto end_span(u64 sequence) -> SpanEndStatus {
        if (auto wrong_thread = verify_thread(); wrong_thread.is_some()) {
            return { .diagnostic = rstd::move(wrong_thread) };
        }

        usize index = active.len();
        for (usize current; current < active.len(); ++current) {
            if (active[current].sequence == sequence) {
                index = current;
                break;
            }
        }
        if (index == active.len()) return { .ended = true };

        auto optional_diagnostic = Option<ProbeDiagnostic> {};
        if (index + usize(1) != active.len()) {
            auto value =
                ProbeDiagnostic::NonLifo(active[active.len() - usize(1)].sequence, sequence);
            remember(value);
            optional_diagnostic = Some(rstd::move(value));
        }

        auto       span     = active.remove(index);
        auto const ended_ns = now_ns();
        if (ended_ns < span.started_ns) {
            auto value = ProbeDiagnostic::ClockStalled();
            remember(value);
            return { .ended = true, .diagnostic = Some(rstd::move(value)) };
        }

        if (config.overflow.is_DropNewest() && samples.len() >= config.sample_capacity) {
            ++dropped_samples;
        } else {
            samples.push(SpanSample {
                .probe           = span.probe,
                .started_ns      = span.started_ns,
                .elapsed         = time::Duration::from_nanos(ended_ns - span.started_ns),
                .recorder_id     = recorder_id,
                .thread_id       = owner_thread,
                .frame           = frame,
                .sequence        = span.sequence,
                .parent_sequence = span.parent_sequence,
            });
        }
        return { .ended = true, .diagnostic = rstd::move(optional_diagnostic) };
    }

    auto take_diagnostic() -> Option<ProbeDiagnostic> { return diagnostic.take(); }

    auto drain_samples() -> ProbeBatch {
        auto replacement = spare.take();
        auto next = replacement.is_some() ? rstd::move(replacement).unwrap_unchecked()
                                          : Vec<SpanSample>::with_capacity(config.sample_capacity);
        auto completed  = rstd::move(samples);
        samples         = rstd::move(next);
        auto dropped    = dropped_samples;
        dropped_samples = usize();
        return ProbeBatch(
            schema.clone(), rstd::move(completed), recorder_id, owner_thread, dropped);
    }
};

export template<typename Clock>
    requires Impled<Clock, MonotonicClock>
class BasicSpanGuard {
    ProbeRecorderState<Clock>* state_ {};
    u64                        sequence_ {};

public:
    BasicSpanGuard(ProbeRecorderState<Clock>* state, u64 sequence) noexcept
        : state_(state), sequence_(sequence) {}
    BasicSpanGuard()                                         = default;
    BasicSpanGuard(const BasicSpanGuard&)                    = delete;
    auto operator=(const BasicSpanGuard&) -> BasicSpanGuard& = delete;

    BasicSpanGuard(BasicSpanGuard&& other) noexcept
        : state_(rstd::exchange(other.state_, nullptr)), sequence_(other.sequence_) {}

    auto operator=(BasicSpanGuard&& other) noexcept -> BasicSpanGuard& {
        if (this != &other) {
            static_cast<void>(finish());
            state_    = rstd::exchange(other.state_, nullptr);
            sequence_ = other.sequence_;
        }
        return *this;
    }

    ~BasicSpanGuard() { static_cast<void>(finish()); }

    auto is_active() const noexcept -> bool { return state_ != nullptr; }

    auto finish() noexcept -> Result<empty, ProbeError> {
        if (state_ == nullptr) return Ok(empty {});
        auto status = state_->end_span(sequence_);
        if (status.ended) state_ = nullptr;
        if (status.diagnostic.is_some()) {
            return Err(ProbeError::Diagnostic(rstd::move(status.diagnostic).unwrap_unchecked()));
        }
        return Ok(empty {});
    }
};

export template<typename Clock>
    requires Impled<Clock, MonotonicClock>
class BasicProbeRecorder {
    Box<ProbeRecorderState<Clock>> state_;

public:
    explicit BasicProbeRecorder(Box<ProbeRecorderState<Clock>> state): state_(rstd::move(state)) {}
    BasicProbeRecorder(const BasicProbeRecorder&)                        = delete;
    auto operator=(const BasicProbeRecorder&) -> BasicProbeRecorder&     = delete;
    BasicProbeRecorder(BasicProbeRecorder&&) noexcept                    = default;
    auto operator=(BasicProbeRecorder&&) noexcept -> BasicProbeRecorder& = default;

    auto span(ProbeId probe) noexcept -> BasicSpanGuard<Clock> {
        auto sequence = state_.get()->begin_span(probe);
        if (sequence.is_none()) return {};
        return BasicSpanGuard<Clock>(state_.get(), *sequence);
    }

    auto begin_frame(u64 frame) -> Result<empty, ProbeError> {
        if (auto wrong_thread = state_.get()->verify_thread(); wrong_thread.is_some()) {
            return Err(ProbeError::Diagnostic(rstd::move(wrong_thread).unwrap_unchecked()));
        }
        if (state_.get()->frame.is_some()) {
            auto value = ProbeDiagnostic::FrameAlreadyActive();
            state_.get()->remember(value);
            return Err(ProbeError::Diagnostic(rstd::move(value)));
        }
        if (! state_.get()->active.is_empty()) {
            auto value = ProbeDiagnostic::ActiveSpansPending(state_.get()->active.len());
            state_.get()->remember(value);
            return Err(ProbeError::Diagnostic(rstd::move(value)));
        }
        state_.get()->frame = Some(frame);
        return Ok(empty {});
    }

    auto end_frame() -> Result<ProbeBatch, ProbeError> {
        if (auto wrong_thread = state_.get()->verify_thread(); wrong_thread.is_some()) {
            return Err(ProbeError::Diagnostic(rstd::move(wrong_thread).unwrap_unchecked()));
        }
        if (state_.get()->frame.is_none()) {
            auto value = ProbeDiagnostic::NoActiveFrame();
            state_.get()->remember(value);
            return Err(ProbeError::Diagnostic(rstd::move(value)));
        }
        if (auto diagnostic = state_.get()->take_diagnostic(); diagnostic.is_some()) {
            return Err(ProbeError::Diagnostic(rstd::move(diagnostic).unwrap_unchecked()));
        }
        if (! state_.get()->active.is_empty()) {
            return Err(ProbeError::Diagnostic(
                ProbeDiagnostic::ActiveSpansPending(state_.get()->active.len())));
        }
        state_.get()->frame = None();
        return Ok(state_.get()->drain_samples());
    }

    auto drain() -> Result<ProbeBatch, ProbeError> {
        if (auto wrong_thread = state_.get()->verify_thread(); wrong_thread.is_some()) {
            return Err(ProbeError::Diagnostic(rstd::move(wrong_thread).unwrap_unchecked()));
        }
        if (state_.get()->frame.is_some()) {
            return Err(ProbeError::Diagnostic(ProbeDiagnostic::FrameStillActive()));
        }
        if (auto diagnostic = state_.get()->take_diagnostic(); diagnostic.is_some()) {
            return Err(ProbeError::Diagnostic(rstd::move(diagnostic).unwrap_unchecked()));
        }
        if (! state_.get()->active.is_empty()) {
            return Err(ProbeError::Diagnostic(
                ProbeDiagnostic::ActiveSpansPending(state_.get()->active.len())));
        }
        return Ok(state_.get()->drain_samples());
    }

    auto recycle(ProbeBatch batch) -> Result<empty, ProbeError> {
        auto batch_schema = batch.schema_owner();
        if (! Arc<ProbeSchema>::ptr_eq(state_.get()->schema, batch_schema)) {
            return Err(ProbeError::SchemaMismatch());
        }
        auto samples = batch.take_samples();
        samples.clear();
        if (state_.get()->spare.is_none()) state_.get()->spare = Some(rstd::move(samples));
        return Ok(empty {});
    }

    auto sample_capacity() const noexcept -> usize { return state_.as_ref()->samples.capacity(); }
    auto recorder_id() const noexcept -> u64 { return state_.as_ref()->recorder_id; }
};

export template<typename Clock>
    requires Impled<Clock, MonotonicClock>
class BasicProbeSession {
    Arc<ProbeSchema>    schema_;
    Arc<Clock>          clock_;
    mutable Atomic<u64> next_recorder_ { u64(1) };

public:
    BasicProbeSession(Arc<ProbeSchema> schema, Clock clock)
        : schema_(rstd::move(schema)), clock_(Arc<Clock>::make(rstd::move(clock))) {}

    static auto new_(Arc<ProbeSchema> schema) -> BasicProbeSession
        requires requires { Clock {}; }
    {
        return BasicProbeSession(rstd::move(schema), Clock {});
    }

    auto recorder(RecorderConfig config = {}) const -> BasicProbeRecorder<Clock> {
        auto const recorder_id = next_recorder_.fetch_add(u64(1), Ordering::Relaxed);
        return BasicProbeRecorder<Clock>(Box<ProbeRecorderState<Clock>>::make(schema_.clone(),
                                                                              clock_.clone(),
                                                                              rstd::move(config),
                                                                              thread::current_id(),
                                                                              recorder_id));
    }

    auto schema() const noexcept -> ref<ProbeSchema> { return schema_.deref(); }
};

export using ProbeSession  = BasicProbeSession<SteadyClock>;
export using ProbeRecorder = BasicProbeRecorder<SteadyClock>;
export using SpanGuard     = BasicSpanGuard<SteadyClock>;

export template<typename Recorder, typename Function>
decltype(auto) measure(Recorder& recorder, ProbeId probe, Function&& function) {
    auto guard = recorder.span(probe);
    return rstd::forward<Function>(function)();
}

} // namespace rstd::bench::probe
