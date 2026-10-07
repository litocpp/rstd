module;
#include <rstd/macro.hpp>

module rstd_benches;
import rstd;
import rstd.bench;

using namespace rstd::prelude;

auto rstd_bench::make_suite() -> Result<rstd::bench::Suite, String> {
    rstd::bench::Suite suite;
    rstd_try(register_alloc(suite));
    rstd_try(register_iter(suite));
    rstd_try(register_slice(suite));
    rstd_try(register_random(suite));
    rstd_try(register_simd(suite));
    rstd_try(register_simd_operations(suite));
    rstd_try(register_sync(suite));
    rstd_try(register_async_runtime(suite));
    rstd_try(register_async_loopback(suite));
    rstd_try(register_async_io(suite));
    rstd_try(register_net(suite));
    rstd_try(add_domain_cases(suite));
    return Ok(rstd::move(suite));
}
