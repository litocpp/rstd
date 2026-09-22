import rstd;

using namespace rstd::literals;

int main() {
    auto borrow = rstd::ref<rstd::str>(rstd::string::String::make("temporary"_str));
    return static_cast<int>(borrow.len());
}
