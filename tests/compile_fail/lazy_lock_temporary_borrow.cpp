import rstd;

int main() {
    auto value = rstd::sync::LazyLock<int>::make([] {
                     return 1;
                 }).force();
    static_cast<void>(value);
}
