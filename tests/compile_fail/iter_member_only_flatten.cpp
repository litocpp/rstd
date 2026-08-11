import rstd;

struct MemberOnly {
    auto into_iter() && { return rstd::iter::once(rstd::i32(1)); }
};

int main() {
    auto flattened = rstd::iter::once(MemberOnly {}).flatten();
    return static_cast<int>(flattened.count().to_primitive());
}
