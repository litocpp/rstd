import rstd;
using namespace rstd::prelude;
using namespace rstd::literals;
auto invalid() {
    return rstd::iter::unique_by(rstd::iter::once(String::make("key"_str)), [](const String& item) {
        return item.as_str();
    });
}
