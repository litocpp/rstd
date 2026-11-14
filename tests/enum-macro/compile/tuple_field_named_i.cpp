import rstd.core;

[[pmacro::invoke("rstd::Enum",
                 enum Event {
                     Value(I : int),
                 })]];

static_assert([] {
    auto value = Event::Value(42);
    return value.as_Value().template get<0>() == value.as_Value().I;
}());
