import rstd;

auto main() -> int {
    auto value = rstd::json::Value::Null();
    return value.is_null() ? 0 : 1;
}
