import rstd;

int main() {
    auto iterator = rstd::iter::range(rstd::i32(), rstd::i32(3));
    auto values   = iterator.collect<rstd::vec::Vec<rstd::i32>>();
    return static_cast<int>(values.len().to_primitive());
}
