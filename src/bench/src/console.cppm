module;
#include <rstd/enum.hpp>

export module rstd.bench:console;
export import :compare;
using namespace rstd::prelude;
export namespace rstd::bench
{
class SummaryValue {
    RSTD_ENUM(SummaryValue,
              (Measured, (f64 value;)),
              (Skipped, (String reason;)),
              (Failed, (String reason;)))
};
struct SummaryRow {
    String       name;
    SummaryValue value;
    String       unit;
};
class SummaryRenderer {
    usize names_ { 4 }, values_ { 12 }, units_ { 4 };
    bool  started_ {};

public:
    void include(ref<str> name, ref<str> unit);
    auto header() -> String;
    auto render(const SummaryRow&) -> Result<String, String>;
};
namespace console
{
auto summary(slice<SummaryRow> rows) -> Result<String, String>;
auto number(f64 value) -> String;
auto time(f64 ns) -> String;
auto bytes(f64 value) -> String;
auto byte_rate(f64 value) -> String;
auto item_rate(f64 value) -> String;
auto escaped(ref<str> value) -> String;
auto exact(const rstd::bench::DiagnosticValue& value) -> String;
auto context(bool                               quick,
             bool                               diagnostic,
             u64                                repeats,
             bool                               verbose,
             const rstd::bench::RunEnvironment& environment) -> String;
auto comparison(const rstd::bench::ComparisonReport& report, bool verbose) -> String;
auto write(ref<str> text) -> Result<empty, String>;
auto write_comparison(const rstd::bench::ComparisonReport& report, bool verbose)
    -> Result<bool, String>;
} // namespace console

struct ConsoleOptions {
    bool verbose {};
    u64  repeats { u64(1) };
    bool use_case_names {};
};

struct ConsoleRateColumn {
    String   name;
    WorkUnit unit;
    String   label;
};
struct ConsoleTable {
    String                 title;
    Vec<ConsoleRateColumn> rates;
};

class ConsoleRenderer {
    struct RateColumn {
        String   name;
        WorkUnit unit;
        String   label;
        usize    width;
    };
    Vec<RateColumn>      rate_columns_;
    String               timing_source_, timing_boundary_, work_label_, resolution_label_;
    ConsoleOptions       options_;
    Option<ConsoleTable> fixed_table_;
    bool                 fixed_started_ {};
    auto                 render_table(const RunResult&) -> String;
    usize                name_width_ { usize(4) };
    int                  table_ {};
    bool                 items_ {};
    bool                 bytes_ {};
    SummaryStatistic     statistic_ = SummaryStatistic::Median;

public:
    explicit ConsoleRenderer(ConsoleOptions options = {}): options_(options) {}
    ConsoleRenderer(ConsoleTable table, ConsoleOptions options = {})
        : options_(options), fixed_table_(Some(rstd::move(table))) {}
    void include(ref<str> name);
    auto render(const rstd::bench::RunResult& result) -> String;
};

template<typename Writer>
struct ConsoleReporter {
    Writer&         writer;
    ConsoleRenderer renderer;
    auto            report(const rstd::bench::RunResult& result) -> Result<empty, String> {
        auto text    = renderer.render(result);
        auto written = rstd::io::write_all(writer, text.as_str().as_bytes());
        if (written.is_err())
            return Err(rstd::format("cannot write benchmark output: {}", written.unwrap_err()));
        return Ok(empty {});
    }
};

} // namespace rstd::bench
