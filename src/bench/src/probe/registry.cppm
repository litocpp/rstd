export module rstd.bench:probe.registry;
export import :probe.model;

using namespace rstd::prelude;
using rstd::sync::Arc;

export namespace rstd::bench::probe
{

class ProbeRegistry {
    Vec<String> labels_;

public:
    static auto new_() -> ProbeRegistry { return {}; }

    auto register_probe(ref<str> label) -> Result<ProbeId, ProbeError> {
        for (usize index; index < labels_.len(); ++index) {
            if (labels_[index].as_str() == label) {
                return Ok(ProbeId::from_u32(u32(index.to_primitive())));
            }
        }
        if (labels_.len().to_primitive() > u32::MAX.to_primitive()) {
            return Err(ProbeError::ProbeIdExhausted());
        }
        auto id = ProbeId::from_u32(u32(labels_.len().to_primitive()));
        labels_.push(String::make(label));
        return Ok(id);
    }

    auto freeze() && -> Arc<ProbeSchema> { return Arc<ProbeSchema>::make(rstd::move(labels_)); }
};

} // namespace rstd::bench::probe
