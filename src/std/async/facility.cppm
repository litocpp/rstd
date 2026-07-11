export module rstd:async.facility;
import rstd.core;

using namespace rstd;

namespace rstd::async
{

class FacilityJob;

enum class FacilityEndpointSubmitResult
{
    Accepted,
    Rejected,
    Unsupported,
};

struct RawFacilityEndpointVTable {
    using SubmitFn = FacilityEndpointSubmitResult (*)(voidp, FacilityJob);
    using DropFn   = void (*)(voidp);

    SubmitFn submit;
    DropFn   drop;
};

struct RawFacilityEndpoint {
    voidp                            data { nullptr };
    const RawFacilityEndpointVTable* vtable { nullptr };

    static auto from_raw_parts(voidp data, const RawFacilityEndpointVTable* vtable) noexcept
        -> RawFacilityEndpoint {
        return RawFacilityEndpoint { data, vtable };
    }
};

class FacilityEndpoint {
    usize               m_id {};
    RawFacilityEndpoint m_raw {};

public:
    FacilityEndpoint() = default;

    FacilityEndpoint(const FacilityEndpoint&)                    = delete;
    auto operator=(const FacilityEndpoint&) -> FacilityEndpoint& = delete;

    FacilityEndpoint(FacilityEndpoint&& other) noexcept
        : m_id(other.m_id), m_raw(rstd::exchange(other.m_raw, {})) {}

    auto operator=(FacilityEndpoint&& other) noexcept -> FacilityEndpoint& {
        if (this != &other) {
            reset();
            m_id  = other.m_id;
            m_raw = rstd::exchange(other.m_raw, {});
        }
        return *this;
    }

    ~FacilityEndpoint() { reset(); }

    static auto from_raw(usize id, RawFacilityEndpoint raw) -> FacilityEndpoint {
        auto endpoint  = FacilityEndpoint {};
        endpoint.m_id  = id;
        endpoint.m_raw = raw;
        return endpoint;
    }

    auto id() const noexcept -> usize { return m_id; }

    auto submit(FacilityJob job) -> FacilityEndpointSubmitResult;

    void reset() {
        auto raw = rstd::exchange(m_raw, {});
        if (raw.vtable != nullptr) {
            raw.vtable->drop(raw.data);
        }
    }
};

} // namespace rstd::async
