module;
#include <rstd/enum.hpp>

export module rstd.bench:diagnostic;
export import :model;

using namespace rstd::prelude;

export namespace rstd::bench
{

enum class DiagnosticUnit
{
    Count,
    Bytes,
    Ratio
};

class DiagnosticValue {
    RSTD_ENUM(DiagnosticValue,
              (Count, (u64 value;)),
              (Ratio, (u64 numerator; u64 denominator;)),
              (Unavailable, (String reason;)),
              (Overflow))
};

struct DiagnosticMetric {
    String          name;
    DiagnosticUnit  unit;
    MetricDirection direction;
    String          scope;
    String          source;
    DiagnosticValue value;
};

struct AllocationDiagnostic {
    rstd::alloc::AllocationWindow operation;
    rstd::alloc::AllocationStats  lifetime;
};

struct AllocationSummary {
    DiagnosticValue allocations;
    DiagnosticValue deallocations;
    DiagnosticValue grows;
    DiagnosticValue shrinks;
    DiagnosticValue failures;
    DiagnosticValue requested_bytes;
    DiagnosticValue allocations_per_op;
    DiagnosticValue bytes_per_op;
    DiagnosticValue before_live;
    DiagnosticValue after_live;
    DiagnosticValue peak_live;
    DiagnosticValue cleanup_live;
};

struct DiagnosticMeasurement {
    u64                          iterations;
    RunConfig                    work;
    MeasurementScope             scope { MeasurementScope::Repeated };
    Option<usize>                batch_size;
    Vec<DiagnosticMetric>        metrics;
    Option<AllocationDiagnostic> allocations;
    String allocation_unavailable { "no tracked allocator was requested"_Str };

    auto validate() const -> Result<empty, String>;
    auto allocation_summary() const -> AllocationSummary;
};

/// One fresh, thread-confined context per session; borrowed allocators must not escape it.
class DiagnosticContext {
    rstd::alloc::AllocationTracker tracker_;
    bool                           tracking_ {};

public:
    template<typename A = ::alloc::Global>
    auto allocator(A underlying = {}) -> rstd::alloc::TrackingAllocator<A> {
        tracking_ = true;
        return { rstd::move(underlying), tracker_ };
    }
    auto tracking() const noexcept -> bool { return tracking_; }
    auto snapshot() const noexcept -> rstd::alloc::AllocationStats { return tracker_.snapshot(); }
    auto begin() -> Result<empty, String>;
    auto end() -> Result<rstd::alloc::AllocationWindow, String>;
};

/// run owns exactly the requested operations and declares their input/output lifetime scope.
template<typename Session>
concept DiagnosticSession = requires(Session& session, u64 iterations) {
    { session.check() } -> mtp::same_as<Result<empty, String>>;
    { session.run(iterations) } -> mtp::same_as<Result<DiagnosticMeasurement, String>>;
    { session.finish() } -> mtp::same_as<Result<empty, String>>;
};

template<typename Make>
class DiagnosticFactory {
    Make make_;

public:
    explicit DiagnosticFactory(Make make): make_(rstd::move(make)) {}
    auto prepare(DiagnosticContext& context) { return make_(context); }
};

template<typename Make>
auto diagnostic_factory(Make make) -> DiagnosticFactory<Make> {
    return DiagnosticFactory<Make>(rstd::move(make));
}

} // namespace rstd::bench
