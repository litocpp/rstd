import rstd;

struct MutOnly {
    using Target = int;

    int* value;

    auto deref_mut() noexcept -> rstd::mut_ref<Target> {
        return rstd::mut_ref<Target>::from_raw_parts(value);
    }
};

int main() {
    static_assert(rstd::Impled<MutOnly, rstd::ops::DerefMut>);
}
