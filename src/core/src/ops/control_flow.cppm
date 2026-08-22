export module rstd.core:ops.control_flow;
export import :choice;

namespace rstd::ops
{

enum class ControlFlowState : rstd::uint8_t
{
    Break,
    Continue,
};

export template<typename BreakType, typename ContinueType = rstd::empty>
class ControlFlow {
    template<typename T>
    using Stored = mtp::cond<mtp::is_ref<T>, mtp::add_ptr<mtp::rm_ref<T>>, T>;

    using Storage = Choice<choice_case<ControlFlowState::Break, Stored<BreakType>>,
                           choice_case<ControlFlowState::Continue, Stored<ContinueType>>>;

    Storage storage_;

    explicit constexpr ControlFlow(Storage storage): storage_(rstd::move(storage)) {}

    template<typename T, typename U>
    static constexpr decltype(auto) store(U&& value) {
        if constexpr (mtp::is_ref<T>)
            return rstd::addressof(value);
        else
            return rstd::forward<U>(value);
    }

    template<typename T, typename U>
    static constexpr decltype(auto) load(U&& value) {
        if constexpr (mtp::is_ref<T>)
            return static_cast<T>(*value);
        else
            return rstd::forward<U>(value);
    }

public:
    using break_type    = BreakType;
    using continue_type = ContinueType;

    template<typename U>
    static constexpr auto Break(U&& value) -> ControlFlow {
        return ControlFlow(Storage::template with<ControlFlowState::Break>(
            store<BreakType>(rstd::forward<U>(value))));
    }

    template<typename U>
    static constexpr auto Continue(U&& value) -> ControlFlow {
        return ControlFlow(Storage::template with<ControlFlowState::Continue>(
            store<ContinueType>(rstd::forward<U>(value))));
    }

    static constexpr auto Continue() -> ControlFlow
        requires mtp::init<ContinueType>
    {
        return Continue(ContinueType {});
    }

    [[nodiscard]]
    constexpr auto is_break() const noexcept -> bool {
        return storage_.template is<ControlFlowState::Break>();
    }

    [[nodiscard]]
    constexpr auto is_continue() const noexcept -> bool {
        return storage_.template is<ControlFlowState::Continue>();
    }

    constexpr decltype(auto) break_value_unchecked() & {
        return load<BreakType>(storage_.template as<ControlFlowState::Break>());
    }

    constexpr decltype(auto) break_value_unchecked() const& {
        return load<BreakType>(storage_.template as<ControlFlowState::Break>());
    }

    constexpr decltype(auto) break_value_unchecked() && {
        return load<BreakType>(rstd::move(storage_).template as<ControlFlowState::Break>());
    }

    constexpr decltype(auto) continue_value_unchecked() & {
        return load<ContinueType>(storage_.template as<ControlFlowState::Continue>());
    }

    constexpr decltype(auto) continue_value_unchecked() const& {
        return load<ContinueType>(storage_.template as<ControlFlowState::Continue>());
    }

    constexpr decltype(auto) continue_value_unchecked() && {
        return load<ContinueType>(rstd::move(storage_).template as<ControlFlowState::Continue>());
    }
};

} // namespace rstd::ops
