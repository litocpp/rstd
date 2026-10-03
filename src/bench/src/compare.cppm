export module rstd.bench:compare;
export import :runner;

using namespace rstd::prelude;

export namespace rstd::bench
{

enum class ComparisonMode
{
    Strict,
    CrossEnvironment
};

struct CaseComparison {
    String      id;
    Option<f64> baseline_ns_per_op;
    Option<f64> candidate_ns_per_op;
    Option<f64> candidate_over_baseline;
    Vec<String> reasons;
};

struct ComparisonReport {
    ComparisonMode      mode;
    Vec<String>         environment_differences;
    Vec<CaseComparison> cases;
};

auto compare(const RunReport& baseline,
             const RunReport& candidate,
             ComparisonMode   mode = ComparisonMode::Strict) -> ComparisonReport;

} // namespace rstd::bench
