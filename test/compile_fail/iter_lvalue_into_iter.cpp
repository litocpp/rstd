import rstd;

int main() {
    auto iterator = rstd::iter::range(rstd::i32(), rstd::i32(3));
    auto copy     = rstd::iter::into_iter(iterator);
    return static_cast<int>(copy.count().to_primitive());
}
