export module rstd.core:stream.traits;
export import :future;
export import :iter.traits;

namespace rstd::stream
{

export using future::Stream;
export using future::stream_item_t;
export using future::poll_next;

/// Values and explicit borrows only. Keep polled state stable while an operation borrows it.
export template<typename S>
concept Source =
    requires { typename stream_item_t<S>; } && iter::valid_item<stream_item_t<S>> &&
    (! mtp::is_void<stream_item_t<S>>) && Impled<mtp::rm_cvf<S>, Stream<stream_item_t<S>>>;

template<typename F>
concept Pollable = (! mtp::is_ref<F>) && requires { typename future::future_output_t<F>; } &&
                   Impled<mtp::rm_cvf<F>, future::Future<future::future_output_t<F>>>;

/// Keep the source alive and unmoved; only one consumer may poll it at a time.
export template<Source S>
class StreamRef {
    S* source_;

public:
    using Item = stream_item_t<S>;
    explicit StreamRef(S& source [[clang::lifetimebound]]) noexcept
        : source_(rstd::addressof(source)) {}
    StreamRef(const StreamRef&)                    = delete;
    auto operator=(const StreamRef&) -> StreamRef& = delete;
    StreamRef(StreamRef&& other) noexcept: source_(rstd::exchange(other.source_, nullptr)) {}
    auto operator=(StreamRef&& other) noexcept -> StreamRef& {
        if (this != &other) source_ = rstd::exchange(other.source_, nullptr);
        return *this;
    }
    auto poll_next(mut_ref<StreamRef> self, task::Context& cx) -> Poll<Option<Item>> {
        if (! self->source_) rstd::panic { "StreamRef polled after move" };
        return future::poll_next(*self->source_, cx);
    }
};

export template<Source S>
    requires(! mtp::is_const<S>)
auto by_ref(S& source [[clang::lifetimebound]]) -> StreamRef<S> {
    return StreamRef<S>(source);
}

export template<Source S>
class Next {
    StreamRef<S> source_;
    bool         completed_ {};

public:
    using Output = Option<stream_item_t<S>>;
    explicit Next(S& source [[clang::lifetimebound]]): source_(source) {}
    auto poll(mut_ref<Next> self, task::Context& cx) -> Poll<Output> {
        if (self->completed_) rstd::panic { "Next polled after completion" };
        auto out         = future::poll_next(self->source_, cx);
        self->completed_ = out.is_ready();
        return out;
    }
};

/// Dropping this waiter does not drop the source or its pending operation.
export template<Source S>
    requires(! mtp::is_const<S>)
auto next(S& source [[clang::lifetimebound]]) -> Next<S> {
    return Next<S>(source);
}

// Bound work within looping consumers without depending on an executor.
class PollBudget {
    unsigned remaining_ { 64 };

public:
    auto proceed(task::Context& cx) -> bool {
        if (remaining_ == 0) {
            cx.waker().wake_by_ref();
            return false;
        }
        --remaining_;
        return true;
    }
};

} // namespace rstd::stream
