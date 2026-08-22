import rstd;

int main() {
    auto borrowed = rstd::iter::range(rstd::i32(), rstd::i32(3)).by_ref();
    return static_cast<int>(rstd::move(borrowed).count().to_primitive());
}
