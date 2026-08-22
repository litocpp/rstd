export module rstd.parse.alloc:source;
export import rstd.parse.core;
export import rstd.alloc;

using namespace rstd::prelude;
using ::alloc::string::String;

export namespace rstd::parse
{

class SourceId {
    String value_;

public:
    SourceId();
    explicit SourceId(ref<str> value);
    explicit SourceId(String value);

    auto as_str() const noexcept [[clang::lifetimebound]] -> ref<str>;
    auto clone() const -> SourceId;
};

} // namespace rstd::parse
