import rstd;

int main() {
    auto borrow = rstd::os::fd::OwnedFd::from_raw_fd(0).as_fd();
    return borrow.as_raw_fd();
}
