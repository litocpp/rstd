namespace
{

struct A {};
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

static void _do() {
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
}

} // namespace