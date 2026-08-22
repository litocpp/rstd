import rstd;

int main() {
    auto values   = rstd::vec::Vec<rstd::i32>::make();
    auto iterator = values.into_iter();
    return static_cast<int>(rstd::move(iterator).count().to_primitive());
}
