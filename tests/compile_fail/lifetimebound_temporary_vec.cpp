import rstd;

int main() {
    auto borrow = rstd::vec::Vec<int>::make().as_slice();
    return static_cast<int>(borrow.len());
}
