import rstd;

int main() {
    auto borrow = rstd::fs::File::from_raw_fd(0).as_fd();
    return borrow.as_raw_fd();
}
