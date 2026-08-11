import rstd;

int main() {
    auto borrow = rstd::boxed::Box<int>::make(42).as_ref();
    return *borrow;
}
