import rstd;
import rstd_benches;

using namespace rstd::prelude;

auto main() -> int {
    auto options =
        rstd_bench::parse_options(rstd::env::args_os().collect<Vec<rstd::ffi::OsString>>());
    if (options.is_err()) {
        rstd::io::eprintln("{}", options.unwrap_err());
        return 2;
    }
    auto result = rstd_bench::execute(*options);
    if (result.is_err()) {
        rstd::io::eprintln("{}", result.unwrap_err());
        return 1;
    }
    return *result ? 0 : 1;
}
