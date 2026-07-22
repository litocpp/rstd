import rstd;

[[gnu::noinline]]
auto initial_value() noexcept -> rstd::i32 {
    return rstd::i32(42);
}

auto guarded_value() noexcept -> rstd::i32& {
    static rstd::i32 value = initial_value();
    return value;
}

int main() {
    auto text = rstd::format("hello {}", "world");
    if (text.as_str() != "hello world") return 1;
    if (guarded_value() != rstd::i32(42) || guarded_value() != rstd::i32(42)) return 2;

    auto values = rstd::collections::HashMap<rstd::i32, rstd::i32>::make();
    values.insert(rstd::i32(7), rstd::i32(11));
    auto value = values.get(rstd::i32(7));
    if (value.is_none() || **value != rstd::i32(11)) return 3;
    return 0;
}
