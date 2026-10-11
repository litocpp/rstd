import rstd.core;

using namespace rstd::prelude;
namespace stream = rstd::stream;
namespace future = rstd::future;
using rstd::task::Poll;
using rstd::task::Context;

struct Twice {
    using Output = int;
    int  value;
    bool pending { true };
    auto poll(mut_ref<Twice> self, Context& cx) -> Poll<int> {
        if (rstd::exchange(self->pending, false)) {
            cx.waker().wake_by_ref();
            return Poll<int>::Pending();
        }
        return Poll<int>::Ready(self->value * 2);
    }
};

template<typename T>
concept StreamItem = requires { typename future::Stream<T>; };
static_assert(! StreamItem<int&>);
static_assert(StreamItem<ref<int>>);

auto main() -> int {
    auto cx    = Context(rstd::task::Waker::noop());
    int  calls = 0;
    auto source =
        stream::then(stream::chain(stream::once(3), stream::once(4)), [&calls](int value) {
            ++calls;
            return Twice { value };
        });
    {
        auto waiting = stream::next(source);
        if (! future::poll(waiting, cx).is_pending() || calls != 1) return 1;
    }
    auto next = stream::next(source);
    auto out  = future::poll(next, cx);
    if (out.is_pending() || rstd::move(out).take().unwrap() != 6 || calls != 1) return 2;
    auto limited = stream::take(stream::by_ref(source), usize(0));
    if (rstd::move(future::poll_next(limited, cx)).take().is_some() || calls != 1) return 3;
    auto fused = stream::fuse(stream::empty<int>());
    for (int i = 0; i != 3; ++i) {
        if (rstd::move(future::poll_next(fused, cx)).take().is_some()) return 4;
    }
    return 0;
}
