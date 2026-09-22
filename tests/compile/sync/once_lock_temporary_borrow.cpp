import rstd;

int main() {
    auto value = rstd::sync::OnceLock<int>::make().get();
    static_cast<void>(value);
}
