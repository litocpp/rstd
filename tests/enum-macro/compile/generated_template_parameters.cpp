import rstd.core;

[[pmacro::invoke("rstd::Enum",
                 template<class Args,
                          class F,
                          class V,
                          class C,
                          class I,
                          class args,
                          class f,
                          class c,
                          class v,
                          class a,
                          class Field>
                 [[derive(Debug)]] enum Generic {
                     [[default]] Empty,
                     Value(Args, F, V, C, I, args, f, c, v, a, Field),
                     Renamed { [[debug(rename("field"))]] value: Field },
                 })]];

using Sample = Generic<int, int, int, int, int, int, int, int, int, int, int>;

static_assert(Sample().is_Empty());
static_assert(Sample::Value(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11).as_Value().template get<10>() == 11);
static_assert(Sample::Renamed(3).as_Renamed().value == 3);
static_assert(rstd::Impled<Sample, rstd::fmt::Debug>);
