import rstd;

int main() {
    auto borrow = rstd::ref<rstd::str>(rstd::string::String::make("temporary"));
    return static_cast<int>(borrow.len());
}
