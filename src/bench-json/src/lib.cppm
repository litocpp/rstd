export module rstd.bench.json;
export import rstd.bench;
export import rstd.json;
using namespace rstd::prelude;
export namespace rstd::bench
{
namespace report
{
auto to_value(const rstd::bench::RunReport& report) -> Result<rstd::json::Value, String>;
auto from_value(const rstd::json::Value& value) -> Result<rstd::bench::RunReport, String>;
auto encode(const rstd::bench::RunReport& report) -> Result<String, String>;
auto decode(ref<str> input) -> Result<rstd::bench::RunReport, String>;
auto read(ref<rstd::path::Path> path) -> Result<rstd::bench::RunReport, String>;
} // namespace report

} // namespace rstd::bench
