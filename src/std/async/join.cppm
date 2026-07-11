export module rstd:async.join;
export import :async.forward;
import rstd.alloc;

using namespace rstd;
using ::alloc::vec::Vec;

namespace rstd::async::detail
{

template<typename F>
using join_output_t = mtp::void_empty_t<future::future_output_t<F>>;

template<typename F>
    requires Impled<mtp::rm_cvf<F>, future::Future<future::future_output_t<F>>>
auto poll_join_slot(F& future_, Option<join_output_t<F>>& slot, task::Context& cx) -> bool {
    if (slot.is_some()) {
        return true;
    }

    auto out = future::poll(future_, cx);
    if (out.is_pending()) {
        return false;
    }

    if constexpr (mtp::is_void<future::future_output_t<F>>) {
        rstd::move(out).take();
        slot = Some(empty {});
    } else {
        slot = Some(rstd::move(out).take());
    }
    return true;
}

} // namespace rstd::async::detail

namespace rstd::async
{

export template<typename F1, typename F2>
    requires Impled<mtp::rm_cvf<F1>, future::Future<future::future_output_t<F1>>> &&
             Impled<mtp::rm_cvf<F2>, future::Future<future::future_output_t<F2>>>
class Join {
    F1                                f1;
    F2                                f2;
    Option<detail::join_output_t<F1>> out1;
    Option<detail::join_output_t<F2>> out2;
    bool                              completed { false };

public:
    using Output = tuple<detail::join_output_t<F1>, detail::join_output_t<F2>>;

    Join(F1 future1, F2 future2): f1(rstd::move(future1)), f2(rstd::move(future2)) {}

    Join(const Join&)                        = delete;
    Join& operator=(const Join&)             = delete;
    Join(Join&&) noexcept                    = default;
    auto operator=(Join&&) noexcept -> Join& = default;

    auto poll(mut_ref<Join> self, task::Context& cx) -> task::Poll<Output> {
        auto& value = *self;
        if (value.completed) {
            rstd::panic { "async::Join polled after completion" };
        }

        auto ready1 = detail::poll_join_slot(value.f1, value.out1, cx);
        auto ready2 = detail::poll_join_slot(value.f2, value.out2, cx);
        if (ready1 && ready2) {
            value.completed = true;
            return task::Poll<Output>::Ready(Output {
                rstd::move(value.out1).unwrap_unchecked(),
                rstd::move(value.out2).unwrap_unchecked(),
            });
        }
        return task::Poll<Output>::Pending();
    }
};

export template<typename F>
    requires Impled<mtp::rm_cvf<F>, future::Future<future::future_output_t<F>>>
class JoinAll {
    Vec<F>                                futures;
    Vec<Option<detail::join_output_t<F>>> outputs;
    usize                                 remaining { 0 };
    bool                                  completed { false };

public:
    using Output = Vec<detail::join_output_t<F>>;

    explicit JoinAll(Vec<F> in)
        : futures(rstd::move(in)),
          outputs(Vec<Option<detail::join_output_t<F>>>::with_capacity(futures.len())),
          remaining(futures.len()) {
        for (usize i = 0; i < remaining; ++i) {
            outputs.push(None<detail::join_output_t<F>>());
        }
    }

    JoinAll(const JoinAll&)                        = delete;
    JoinAll& operator=(const JoinAll&)             = delete;
    JoinAll(JoinAll&&) noexcept                    = default;
    auto operator=(JoinAll&&) noexcept -> JoinAll& = default;

    auto poll(mut_ref<JoinAll> self, task::Context& cx) -> task::Poll<Output> {
        auto& value = *self;
        if (value.completed) {
            rstd::panic { "async::JoinAll polled after completion" };
        }

        for (usize i = 0; i < value.futures.len(); ++i) {
            if (value.outputs[i].is_some()) {
                continue;
            }
            if (detail::poll_join_slot(value.futures[i], value.outputs[i], cx)) {
                --value.remaining;
            }
        }

        if (value.remaining == 0) {
            value.completed = true;
            auto out        = Output::with_capacity(value.outputs.len());
            for (usize i = 0; i < value.outputs.len(); ++i) {
                out.push(rstd::move(value.outputs[i]).unwrap_unchecked());
            }
            return task::Poll<Output>::Ready(rstd::move(out));
        }

        return task::Poll<Output>::Pending();
    }
};

export template<typename F1, typename F2>
    requires Impled<mtp::rm_cvf<F1>, future::Future<future::future_output_t<F1>>> &&
             Impled<mtp::rm_cvf<F2>, future::Future<future::future_output_t<F2>>>
auto join(F1 future1, F2 future2) -> Join<F1, F2> {
    return Join<F1, F2> { rstd::move(future1), rstd::move(future2) };
}

export template<typename F>
    requires Impled<mtp::rm_cvf<F>, future::Future<future::future_output_t<F>>>
auto join_all(Vec<F> futures) -> JoinAll<F> {
    return JoinAll<F> { rstd::move(futures) };
}

} // namespace rstd::async
