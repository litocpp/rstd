import rstd;

struct MutOnly {
    using Target = int;

    int* value;

    auto deref_mut() noexcept -> rstd::mut_ref<Target> {
        return rstd::mut_ref<Target>::from_raw_parts(value);
    }
};

template<>
struct rstd::Impl<rstd::ops::DerefMut, MutOnly>
    : rstd::LinkClassMethod<rstd::ops::DerefMut, MutOnly> {};

int main() {
    static_assert(rstd::Impled<MutOnly, rstd::ops::DerefMut>);
}
