import rstd;

struct MutOnly {
    int* value;

    auto deref_mut() noexcept -> rstd::mut_ref<int> {
        return rstd::mut_ref<int>::from_raw_parts(value);
    }
};

int main() {
    static_assert(rstd::Impled<MutOnly, rstd::ops::DerefMut>);
}
