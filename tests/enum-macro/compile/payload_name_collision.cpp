import rstd.core;

[[pmacro::invoke("rstd::Enum",
                 enum A_B {
                     C(int),
                 })]];
[[pmacro::invoke("rstd::Enum",
                 enum A {
                     B_C(int),
                 })]];

static_assert(A_B::C(3).as_C().field_0 == 3);
static_assert(A::B_C(4).as_B_C().field_0 == 4);
