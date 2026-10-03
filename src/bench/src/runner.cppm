export module rstd.bench:runner;
export import :suite;

using namespace rstd::prelude;

export namespace rstd::bench
{

struct RunnerOptions {
    Option<String> suite;
    Option<String> id;
    Option<String> name;
    BenchConfig    sampling;
    bool           quick {};
    bool           stop_on_failure {};
    u64            repeats { u64(1) };

    auto validate() const -> Result<empty, String>;
    auto matches(const CaseDescriptor& descriptor) const -> bool;
    auto config_for(const CaseDescriptor& descriptor) const -> BenchConfig;
};

struct RunResult {
    CaseDescriptor descriptor;
    CaseOutcome    outcome;
    u64            repetition;

    auto validate() const -> Result<empty, String>;
};

enum class RunMode
{
    Timing,
    Diagnostic
};

struct EnvironmentField {
    String         name;
    Option<String> value;
    String         source;
};

struct RunEnvironment {
    Vec<EnvironmentField> fields;
    auto                  clone() const -> RunEnvironment;
    auto                  validate() const -> Result<empty, String>;
};

struct RunReport {
    Vec<RunResult> results;
    bool           quick {};
    RunEnvironment environment;
    u32            process_id;
    RunMode        mode { RunMode::Timing };

    auto passed() const -> bool;
    auto validate() const -> Result<empty, String>;
};

struct Reporter {
    template<typename Self, typename = void>
    struct Api {
        using Trait = Reporter;
        auto report(const RunResult& result) -> Result<empty, String> {
            return trait_call<0>(this, result);
        }
    };
    template<typename T>
    using Funcs = TraitFuncs<&T::report>;
};

struct NullReporter {
    auto report(const RunResult&) -> Result<empty, String> { return Ok(empty {}); }
};

struct TextReporter {
    auto report(const RunResult& result) -> Result<empty, String>;
};

class Runner {
    RunnerOptions  options_;
    RunEnvironment environment_;

    static auto checked_report(RunReport report) -> Result<RunReport, String> {
        auto valid = report.validate();
        if (valid.is_err()) return Err(rstd::move(valid).unwrap_err());
        return Ok(rstd::move(report));
    }

public:
    explicit Runner(RunnerOptions options = {}, RunEnvironment environment = {})
        : options_(rstd::move(options)), environment_(rstd::move(environment)) {}

    template<typename Engine>
    auto select(const BasicSuite<Engine>& suite) const -> Result<Vec<usize>, String> {
        auto valid = options_.validate();
        if (valid.is_err()) return Err(rstd::move(valid).unwrap_err());
        auto environment = environment_.validate();
        if (environment.is_err()) return Err(rstd::move(environment).unwrap_err());
        auto selected = Vec<usize>::make();
        for (usize i; i < suite.len(); ++i)
            if (options_.matches(suite.descriptor(i))) selected.push(usize(i));
        if (selected.is_empty()) return Err(String::make("no benchmark cases selected"_str));
        return Ok(rstd::move(selected));
    }

    template<typename Engine, typename MakeEngine, typename Sink>
        requires Impled<Sink, Reporter>
    auto run_with(BasicSuite<Engine>& suite, MakeEngine make_engine, Sink& sink)
        -> Result<RunReport, String> {
        auto selected = select(suite);
        if (selected.is_err()) return Err(rstd::move(selected).unwrap_err());
        RunReport report { .quick       = options_.quick,
                           .environment = environment_.clone(),
                           .process_id  = rstd::process::id() };
        for (u64 repetition; repetition < options_.repeats; ++repetition) {
            for (auto index : *selected) {
                auto      engine  = make_engine(options_.config_for(suite.descriptor(index)));
                auto      outcome = suite.execute(index, engine);
                auto      failed  = outcome.is_Failed();
                RunResult result { suite.descriptor(index).clone(),
                                   rstd::move(outcome),
                                   repetition };
                auto      valid = result.validate();
                if (valid.is_err()) return Err(rstd::move(valid).unwrap_err());
                if (result.outcome.is_Diagnosed())
                    return Err("timing case returned a diagnostic"_Str);
                auto written = as<Reporter>(sink).report(result);
                if (written.is_err()) return Err(rstd::move(written).unwrap_err());
                report.results.push(rstd::move(result));
                if (failed && options_.stop_on_failure) return checked_report(rstd::move(report));
            }
        }
        return checked_report(rstd::move(report));
    }

    template<typename Engine, typename Sink>
        requires Impled<Sink, Reporter>
    auto diagnose(BasicSuite<Engine>& suite, u64 iterations, Sink& sink)
        -> Result<RunReport, String> {
        if (iterations == u64()) return Err("diagnostic iterations must be nonzero"_Str);
        auto selected = select(suite);
        if (selected.is_err()) return Err(rstd::move(selected).unwrap_err());
        RunReport report { .quick       = options_.quick,
                           .environment = environment_.clone(),
                           .process_id  = rstd::process::id(),
                           .mode        = RunMode::Diagnostic };
        for (u64 repetition; repetition < options_.repeats; ++repetition) {
            for (auto index : *selected) {
                RunResult result { suite.descriptor(index).clone(),
                                   suite.execute_diagnostic(index, iterations),
                                   repetition };
                auto      valid = result.validate();
                if (valid.is_err()) return Err(rstd::move(valid).unwrap_err());
                auto written = as<Reporter>(sink).report(result);
                if (written.is_err()) return Err(rstd::move(written).unwrap_err());
                bool failed = result.outcome.is_Failed();
                report.results.push(rstd::move(result));
                if (failed && options_.stop_on_failure) return checked_report(rstd::move(report));
            }
        }
        return checked_report(rstd::move(report));
    }

    template<typename Sink>
        requires Impled<Sink, Reporter>
    auto run(Suite& suite, Sink& sink) -> Result<RunReport, String> {
        return run_with(
            suite,
            [](BenchConfig config) {
                return Bench::new_(rstd::move(config));
            },
            sink);
    }
};

} // namespace rstd::bench
