import rstd;

int main() {
    auto iterator = rstd::iter::range(rstd::i32(), rstd::i32(3));
    auto mapped   = iterator.map([](rstd::i32 value) {
        return value;
    });
    return static_cast<int>(rstd::move(mapped).count().to_primitive());
}
