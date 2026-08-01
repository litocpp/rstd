export module rstd:async.select;
export import :async.forward;

using namespace rstd;

template<typename F>
using select_output_t = mtp::void_empty_t<future::future_output_t<F>>;

template<typename F>
    requires Impled<mtp::rm_cvf<F>, future::Future<future::future_output_t<F>>>
auto take_ready(task::Poll<future::future_output_t<F>>&& out) -> select_output_t<F> {
    if constexpr (mtp::is_void<future::future_output_t<F>>) {
        rstd::move(out).take();
        return empty {};
    } else {
        return rstd::move(out).take();
    }
}

namespace rstd::async
{

export template<typename L, typename R>
class Either {
public:
    enum class Tag
    {
        Left,
        Right,
    };

private:
    Tag       tag_;
    Option<L> left_;
    Option<R> right_;

    Either(Tag tag, Option<L> left, Option<R> right)
        : tag_(tag), left_(rstd::move(left)), right_(rstd::move(right)) {}

public:
    static auto Left(L value) -> Either {
        return Either { Tag::Left, Some(rstd::move(value)), None<R>() };
    }

    static auto Right(R value) -> Either {
        return Either { Tag::Right, None<L>(), Some(rstd::move(value)) };
    }

    auto tag() const noexcept -> Tag { return tag_; }
    auto is_left() const noexcept -> bool { return tag_ == Tag::Left; }
    auto is_right() const noexcept -> bool { return tag_ == Tag::Right; }

    auto unwrap_left() && -> L {
        if (! is_left()) {
            rstd::panic { "Either::unwrap_left called on Right" };
        }
        return rstd::move(left_).unwrap_unchecked();
    }

    auto unwrap_right() && -> R {
        if (! is_right()) {
            rstd::panic { "Either::unwrap_right called on Left" };
        }
        return rstd::move(right_).unwrap_unchecked();
    }
};

export template<typename F1, typename F2>
    requires Impled<mtp::rm_cvf<F1>, future::Future<future::future_output_t<F1>>> &&
             Impled<mtp::rm_cvf<F2>, future::Future<future::future_output_t<F2>>>
class Select {
    Option<F1> f1;
    Option<F2> f2;
    bool       completed { false };

public:
    using LeftOutput  = select_output_t<F1>;
    using RightOutput = select_output_t<F2>;
    using Output      = Either<LeftOutput, RightOutput>;

    Select(F1 future1, F2 future2): f1(Some(rstd::move(future1))), f2(Some(rstd::move(future2))) {}

    Select(const Select&)                        = delete;
    Select& operator=(const Select&)             = delete;
    Select(Select&&) noexcept                    = default;
    auto operator=(Select&&) noexcept -> Select& = default;

    auto poll(mut_ref<Select> self, task::Context& cx) -> task::Poll<Output> {
        auto& value = *self;
        if (value.completed) {
            rstd::panic { "async::Select polled after completion" };
        }

        auto out1 = future::poll(*value.f1, cx);
        if (out1.is_ready()) {
            auto selected   = take_ready<F1>(rstd::move(out1));
            value.completed = true;
            value.f1        = None<F1>();
            value.f2        = None<F2>();
            return task::Poll<Output>::Ready(Output::Left(rstd::move(selected)));
        }

        auto out2 = future::poll(*value.f2, cx);
        if (out2.is_ready()) {
            auto selected   = take_ready<F2>(rstd::move(out2));
            value.completed = true;
            value.f1        = None<F1>();
            value.f2        = None<F2>();
            return task::Poll<Output>::Ready(Output::Right(rstd::move(selected)));
        }

        return task::Poll<Output>::Pending();
    }
};

export template<typename F1, typename F2>
    requires Impled<mtp::rm_cvf<F1>, future::Future<future::future_output_t<F1>>> &&
             Impled<mtp::rm_cvf<F2>, future::Future<future::future_output_t<F2>>>
class Race {
    Select<F1, F2> select_;
    bool           completed { false };

public:
    using Output = select_output_t<F1>;

    static_assert(mtp::same_as<Output, select_output_t<F2>>,
                  "async::race requires both futures to produce the same output type");

    Race(F1 future1, F2 future2): select_(rstd::move(future1), rstd::move(future2)) {}

    Race(const Race&)                        = delete;
    Race& operator=(const Race&)             = delete;
    Race(Race&&) noexcept                    = default;
    auto operator=(Race&&) noexcept -> Race& = default;

    auto poll(mut_ref<Race> self, task::Context& cx) -> task::Poll<Output> {
        auto& value = *self;
        if (value.completed) {
            rstd::panic { "async::Race polled after completion" };
        }

        auto out = future::poll(value.select_, cx);
        if (out.is_pending()) {
            return task::Poll<Output>::Pending();
        }

        value.completed = true;
        auto selected   = rstd::move(out).take();
        if (selected.is_left()) {
            return task::Poll<Output>::Ready(rstd::move(selected).unwrap_left());
        }
        return task::Poll<Output>::Ready(rstd::move(selected).unwrap_right());
    }
};

export template<typename F1, typename F2>
    requires Impled<mtp::rm_cvf<F1>, future::Future<future::future_output_t<F1>>> &&
             Impled<mtp::rm_cvf<F2>, future::Future<future::future_output_t<F2>>>
auto select(F1 future1, F2 future2) -> Select<F1, F2> {
    return Select<F1, F2> { rstd::move(future1), rstd::move(future2) };
}

export template<typename F1, typename F2>
    requires Impled<mtp::rm_cvf<F1>, future::Future<future::future_output_t<F1>>> &&
             Impled<mtp::rm_cvf<F2>, future::Future<future::future_output_t<F2>>>
auto race(F1 future1, F2 future2) -> Race<F1, F2> {
    return Race<F1, F2> { rstd::move(future1), rstd::move(future2) };
}

} // namespace rstd::async
