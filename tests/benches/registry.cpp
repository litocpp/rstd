module rstd_benches;
import rstd;
import rstd.bench;
using namespace rstd::prelude;
auto rstd_bench::make_suite() -> Result<rstd::bench::Suite, String> {
    rstd::bench::Suite suite;
    auto               alloc = register_alloc(suite);
    if (alloc.is_err()) return Err(rstd::move(alloc).unwrap_err());
    auto slice = register_slice(suite);
    if (slice.is_err()) return Err(rstd::move(slice).unwrap_err());
    auto domains = add_domain_cases(suite);
    if (domains.is_err()) return Err(rstd::move(domains).unwrap_err());
    return Ok(rstd::move(suite));
}
