import rstd;

auto escaped_range() {
    auto iterator = rstd::iter::range(rstd::i32(), rstd::i32(3));
    return rstd::iter::for_range(iterator);
}

int main() {
    auto range = escaped_range();
    return range.begin() == range.end();
}
