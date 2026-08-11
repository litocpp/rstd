import rstd;

int main() {
    auto borrow = rstd::array<int, 2> { 1, 2 }.as_slice();
    return static_cast<int>(borrow.len());
}
