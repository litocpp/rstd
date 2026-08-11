import rstd;

struct Key {};

auto main() -> int {
    auto map = rstd::collections::HashMap<Key, int>::make();
    (void)map.insert(Key {}, 1);
}
