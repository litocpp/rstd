import rstd;

struct Value {
    int value;
};

template<>
struct rstd::Impl<rstd::clone::Clone, Value> : rstd::DefaultInImpl<rstd::clone::Clone, Value> {
    auto clone() const -> Value { return this->self(); }
};

auto escaped_proxy() {
    Value value { 42 };
    return rstd::as<rstd::clone::Clone>(value);
}

int main() {
    auto proxy = escaped_proxy();
    return proxy.clone().value;
}
