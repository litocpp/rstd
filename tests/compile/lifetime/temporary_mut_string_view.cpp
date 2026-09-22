import rstd;

using namespace rstd::literals;

int main() {
    auto borrow = rstd::string::String::make("temporary"_str).as_mut_str();
    return static_cast<int>(borrow.size().to_primitive());
}
