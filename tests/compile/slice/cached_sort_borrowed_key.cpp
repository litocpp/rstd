import rstd;
using namespace rstd::prelude;
using namespace rstd::literals;
void invalid() {
    auto values = rstd::iter::once(String::make("key"_str)).collect<Vec<String>>();
    rstd::slice_::sort_by_cached_key(values.as_mut_slice().as_mut_ref(), [](const String& value) {
        return value.as_str();
    });
}
