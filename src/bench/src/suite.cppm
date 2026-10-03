module;
#include <rstd/enum.hpp>

export module rstd.bench:suite;
export import :workload;
export import :diagnostic;

using namespace rstd::prelude;

export namespace rstd::bench
{

class ParameterValue {
    RSTD_ENUM(ParameterValue,
              (Unsigned, (u64 value;)),
              (Signed, (i64 value;)),
              (Real, (f64 value;)),
              (Boolean, (bool value;)),
              (Text, (String value;)))
public:
    auto clone() const -> ParameterValue;
};

struct Parameter {
    String         name;
    ParameterValue value;
};

struct DatasetDescriptor {
    String      id;
    String      revision;
    String      digest_algorithm;
    String      digest;
    u64         input_bytes;
    Option<u64> seed;
};

struct CaseDescriptor {
    String                    id;
    String                    suite;
    String                    revision;
    String                    implementation;
    Vec<Parameter>            parameters;
    Option<DatasetDescriptor> dataset;
    u64                       quick_iterations { u64(1) };
    String                    name;

    auto validate() const -> Result<empty, String>;
    auto clone() const -> CaseDescriptor;
    auto same_workload(const CaseDescriptor& other) const -> bool;
};

class CaseOutcome {
    RSTD_ENUM(CaseOutcome,
              (Passed, (BenchmarkResult measurement;)),
              (Diagnosed, (DiagnosticMeasurement measurement;)),
              (Failed, (Vec<String> errors;)),
              (Skipped, (String reason;)))
};

auto describe_error(const BenchError& error) -> String;
auto failed(String reason, Result<empty, String> cleanup = Ok(empty {})) -> CaseOutcome;
auto complete_measurement(Result<BenchmarkResult, BenchError> measurement,
                          Result<empty, String>               validation,
                          Result<empty, String> cleanup = Ok(empty {})) -> CaseOutcome;

template<typename Session, typename Engine>
concept BenchmarkSession = requires(Session& session, Engine& engine, ref<str> name) {
    { session.check() } -> mtp::same_as<Result<empty, String>>;
    { session.run(engine, name) } -> mtp::same_as<Result<BenchmarkResult, BenchError>>;
    { session.finish() } -> mtp::same_as<Result<empty, String>>;
};

template<typename Engine = Bench>
class BasicSuite {
    using EntryFn      = FnMut<CaseOutcome(Engine&, ref<str>)>;
    using DiagnosticFn = FnMut<CaseOutcome(u64)>;
    struct Entry {
        CaseDescriptor                 descriptor;
        Box<dyn<EntryFn>>              execute;
        Option<Box<dyn<DiagnosticFn>>> diagnose;
    };
    Vec<Entry> entries_;

public:
    auto len() const noexcept -> usize { return entries_.len(); }
    auto descriptor(usize index) const -> const CaseDescriptor& {
        return entries_[index].descriptor;
    }

    template<typename Factory>
    auto add(CaseDescriptor descriptor, Factory factory) -> Result<empty, String> {
        auto execute = [factory = rstd::move(factory)](Engine&  engine,
                                                       ref<str> name) mutable -> CaseOutcome {
            auto prepared = factory.prepare();
            if (prepared.is_err()) return failed(rstd::move(prepared).unwrap_err());
            auto session = rstd::move(prepared).unwrap();
            static_assert(BenchmarkSession<decltype(session), Engine>);
            auto checked = session.check();
            if (checked.is_err()) {
                auto cleaned = session.finish();
                return failed(rstd::move(checked).unwrap_err(), rstd::move(cleaned));
            }
            auto measured = session.run(engine, name);
            auto finished = session.finish();
            return complete_measurement(rstd::move(measured), Ok(empty {}), rstd::move(finished));
        };
        return add_function(rstd::move(descriptor), rstd::move(execute));
    }

    /// The function owns the entire case lifecycle, including validation and cleanup.
    template<typename Function>
        requires requires(Function& function, Engine& engine, ref<str> name) {
            { function(engine, name) } -> mtp::same_as<CaseOutcome>;
        }
    auto add_function(CaseDescriptor descriptor, Function function) -> Result<empty, String> {
        auto valid = descriptor.validate();
        if (valid.is_err()) return valid;
        for (const auto& entry : entries_) {
            if (entry.descriptor.id == descriptor.id)
                return Err(String::make("duplicate benchmark id"_str));
        }
        auto callable = [function = rstd::move(function)](Engine&  engine,
                                                          ref<str> name) mutable -> CaseOutcome {
            return function(engine, name);
        };
        entries_.push(
            Entry { rstd::move(descriptor), Box<dyn<EntryFn>>::make(rstd::move(callable)) });
        return Ok(empty {});
    }

    auto execute(usize index, Engine& engine) -> CaseOutcome {
        auto& entry = entries_[index];
        return entry.execute->operator()(engine, entry.descriptor.id.as_str());
    }

    /// Registers an independent session factory against an existing workload identity.
    template<typename Factory>
    auto add_diagnostic(ref<str> id, Factory factory) -> Result<empty, String> {
        for (auto& entry : entries_) {
            if (entry.descriptor.id != id) continue;
            if (entry.diagnose.is_some()) return Err("duplicate case diagnostic"_Str);
            auto execute = [factory = rstd::move(factory)](u64 iterations) mutable -> CaseOutcome {
                DiagnosticContext context;
                auto              outcome = [&]() -> CaseOutcome {
                    auto prepared = factory.prepare(context);
                    if (prepared.is_err()) return failed(rstd::move(prepared).unwrap_err());
                    auto session = rstd::move(prepared).unwrap();
                    static_assert(DiagnosticSession<decltype(session)>);
                    auto checked = session.check();
                    if (checked.is_err()) {
                        auto finished = session.finish();
                        return failed(rstd::move(checked).unwrap_err(), rstd::move(finished));
                    }
                    auto started = context.begin();
                    if (started.is_err()) {
                        auto finished = session.finish();
                        return failed(rstd::move(started).unwrap_err(), rstd::move(finished));
                    }
                    auto        measured = session.run(iterations);
                    auto        window   = context.end();
                    auto        finished = session.finish();
                    Vec<String> errors;
                    if (measured.is_err()) errors.push(rstd::move(measured).unwrap_err());
                    if (window.is_err()) errors.push(rstd::move(window).unwrap_err());
                    if (finished.is_err()) errors.push(rstd::move(finished).unwrap_err());
                    if (! errors.is_empty()) return CaseOutcome::Failed(rstd::move(errors));
                    auto result       = rstd::move(measured).unwrap();
                    result.iterations = iterations;
                    if (context.tracking()) {
                        result.allocations            = Some(AllocationDiagnostic { *window, {} });
                        result.allocation_unavailable = {};
                    } else
                        result.allocations = None();
                    return CaseOutcome::Diagnosed(rstd::move(result));
                }();
                // Session destruction is included in lifetime accounting, not the operation window.
                if (outcome.is_Diagnosed()) {
                    auto& measured = outcome.as_Diagnosed().measurement;
                    if (measured.allocations.is_some())
                        measured.allocations->lifetime = context.snapshot();
                    auto valid = measured.validate();
                    if (valid.is_err()) return failed(rstd::move(valid).unwrap_err());
                }
                return outcome;
            };
            entry.diagnose = Some(Box<dyn<DiagnosticFn>>::make(rstd::move(execute)));
            return Ok(empty {});
        }
        return Err("diagnostic requires an existing case id"_Str);
    }

    auto execute_diagnostic(usize index, u64 iterations) -> CaseOutcome {
        if (iterations == u64()) return failed("diagnostic iterations must be nonzero"_Str);
        auto& entry = entries_[index];
        if (entry.diagnose.is_none())
            return CaseOutcome::Skipped(
                "independent resource diagnostic is not supported by this case"_Str);
        return (*entry.diagnose)->operator()(iterations);
    }
};

using Suite = BasicSuite<>;

} // namespace rstd::bench
