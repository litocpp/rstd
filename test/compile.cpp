#include <utility>
namespace
{

// trivially
struct A {};
// non-trivially
struct B {
    B() {}
    ~B() {}
    B(const B&) {}
    B(B&&) noexcept {}
    B& operator=(const B&) { return *this; }
    B& operator=(B&&) noexcept { return *this; }

private:
    A a;
};

struct NoCopyMove {
    NoCopyMove() {}
    NoCopyMove(const NoCopyMove&)                = delete;
    NoCopyMove(NoCopyMove&&)                     = delete;
    NoCopyMove& operator=(const NoCopyMove&)     = delete;
    NoCopyMove& operator=(NoCopyMove&&) noexcept = delete;
};

static A i {};

template<int I>
static auto _auto_check() {
    if constexpr (I == 0) {
        return i;
    } else if constexpr (I == 1) {
        return (i);
    } else if constexpr (I == 2) {
        return static_cast<A&>(i);
    } else if constexpr (I == 3) {
        return static_cast<A&&>(i);
    } else {
        return (A {});
    }
}

template<int I>
static auto& _auto_ll_check() {
    if constexpr (I == 0) {
        return i;
    } else if constexpr (I == 1) {
        return (i);
    } else if constexpr (I == 2) {
        return static_cast<A&>(i);
    } else if constexpr (I == 3) {
        return static_cast<A&&>(i);
    } else {
        return (A {});
    }
}

template<int I>
static auto&& _auto_rr_check() {
    if constexpr (I == 0) {
        return i;
    } else if constexpr (I == 1) {
        return (i);
    } else if constexpr (I == 2) {
        return static_cast<A&>(i);
    } else if constexpr (I == 3) {
        return static_cast<A&&>(i);
    } else {
        return (A {});
    }
}

template<int I>
static decltype(auto) _dt_auto_check() {
    if constexpr (I == 0) {
        return i;
    } else if constexpr (I == 1) {
        return (i);
    } else if constexpr (I == 2) {
        return static_cast<A&>(i);
    } else if constexpr (I == 3) {
        return static_cast<A&&>(i);
    } else {
        return (A {});
    }
}

template<typename T>
T _forward(T&&) {}

template<typename T>
T _tcall(T) {}

static void _do() {
    A          a;
    B          b;
    NoCopyMove n;
    A (*x)();
    A& (*x_left)();
    A && (*x_right)();
    x = _auto_check<0>;
    x = _auto_check<1>;
    x = _auto_check<2>;
    x = _auto_check<3>;
    x = _auto_check<4>;

    x_left = _auto_ll_check<0>;
    x_left = _auto_ll_check<1>;
    x_left = _auto_ll_check<2>;
    // _auto_ll_check<3>;
    // _auto_ll_check<4>;

    // _auto_rr_check<0>;
    // _auto_rr_check<1>;
    // _auto_rr_check<2>;
    x_right = _auto_rr_check<3>;
    x_right = _auto_rr_check<4>;

    x       = _dt_auto_check<0>;
    x_left  = _dt_auto_check<1>;
    x_left  = _dt_auto_check<2>;
    x_right = _dt_auto_check<3>;
    x       = _dt_auto_check<4>;

    static_assert(std::same_as<decltype(_forward(a)), A&>);
    static_assert(std::same_as<decltype(_forward(A {})), A>);

    static_assert(std::same_as<decltype(_tcall(b)), B>);
    static_assert(std::same_as<decltype(_tcall(static_cast<B&&>(b))), B>);
    static_assert(std::same_as<decltype(_tcall(B {})), B>);

    static_assert(std::is_trivially_copy_constructible_v<A&>);
    static_assert(std::is_trivially_copy_constructible_v<B&>);
    static_assert(std::is_trivially_copy_constructible_v<A>);
    static_assert(! std::is_trivially_copy_constructible_v<B>);

    static_assert(std::same_as<std::add_rvalue_reference_t<B&>, B&>);
}

} // namespace