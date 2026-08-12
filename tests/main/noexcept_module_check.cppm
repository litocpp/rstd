module;
#include <rstd/enum.hpp>

export module rstd.tests.noexcept_module_check;

import rstd.core;

export namespace noexcept_module_check
{

struct NoThrowPayload {
    int value {};

    NoThrowPayload() noexcept = default;
    explicit NoThrowPayload(int value) noexcept: value(value) {}
    NoThrowPayload(const NoThrowPayload& other) noexcept: value(other.value) {}
    NoThrowPayload(NoThrowPayload&& other) noexcept: value(other.value) {}
    auto operator=(const NoThrowPayload& other) noexcept -> NoThrowPayload& {
        value = other.value;
        return *this;
    }
    auto operator=(NoThrowPayload&& other) noexcept -> NoThrowPayload& {
        value = other.value;
        return *this;
    }
};

struct ThrowingPayload {
    int value {};

    ThrowingPayload() noexcept(false) {}
    explicit ThrowingPayload(int value) noexcept(false): value(value) {}
    ThrowingPayload(const ThrowingPayload& other) noexcept(false): value(other.value) {}
    ThrowingPayload(ThrowingPayload&& other) noexcept(false): value(other.value) {}
    auto operator=(const ThrowingPayload& other) noexcept(false) -> ThrowingPayload& {
        value = other.value;
        return *this;
    }
    auto operator=(ThrowingPayload&& other) noexcept(false) -> ThrowingPayload& {
        value = other.value;
        return *this;
    }
};

struct ThrowingDrop {
    ~ThrowingDrop() noexcept(false) {}
};

static_assert(rstd::mtp::triv_init<int> == rstd::mtp::triv_init_v<int>);
static_assert(rstd::mtp::triv_init<NoThrowPayload> == rstd::mtp::triv_init_v<NoThrowPayload>);
static_assert(rstd::mtp::noex_init<NoThrowPayload> == rstd::mtp::noex_init_v<NoThrowPayload>);
static_assert(rstd::mtp::noex_init<ThrowingPayload> == rstd::mtp::noex_init_v<ThrowingPayload>);
static_assert(rstd::mtp::noex_assign<NoThrowPayload&, const NoThrowPayload&> ==
              rstd::mtp::noex_assign_v<NoThrowPayload&, const NoThrowPayload&>);
static_assert(rstd::mtp::noex_assign<ThrowingPayload&, const ThrowingPayload&> ==
              rstd::mtp::noex_assign_v<ThrowingPayload&, const ThrowingPayload&>);
static_assert(rstd::mtp::noex_copy<NoThrowPayload> == rstd::mtp::noex_copy_v<NoThrowPayload>);
static_assert(rstd::mtp::noex_copy<ThrowingPayload> == rstd::mtp::noex_copy_v<ThrowingPayload>);
static_assert(rstd::mtp::noex_move<NoThrowPayload> == rstd::mtp::noex_move_v<NoThrowPayload>);
static_assert(rstd::mtp::noex_move<ThrowingPayload> == rstd::mtp::noex_move_v<ThrowingPayload>);
static_assert(rstd::mtp::noex_assign_copy<NoThrowPayload> ==
              rstd::mtp::noex_assign_copy_v<NoThrowPayload>);
static_assert(rstd::mtp::noex_assign_copy<ThrowingPayload> ==
              rstd::mtp::noex_assign_copy_v<ThrowingPayload>);
static_assert(rstd::mtp::noex_assign_move<NoThrowPayload> ==
              rstd::mtp::noex_assign_move_v<NoThrowPayload>);
static_assert(rstd::mtp::noex_assign_move<ThrowingPayload> ==
              rstd::mtp::noex_assign_move_v<ThrowingPayload>);
static_assert(rstd::mtp::noex_drop<NoThrowPayload> == rstd::mtp::noex_drop_v<NoThrowPayload>);
static_assert(rstd::mtp::noex_drop<ThrowingDrop> == rstd::mtp::noex_drop_v<ThrowingDrop>);

enum class ChoiceTag
{
    Value
};

using NoThrowChoice  = rstd::Choice<rstd::choice_case<ChoiceTag::Value, NoThrowPayload>>;
using ThrowingChoice = rstd::Choice<rstd::choice_case<ChoiceTag::Value, ThrowingPayload>>;

class NoThrowEnum final {
    RSTD_ENUM(NoThrowEnum, (Value, (NoThrowPayload value;)))
};

class ThrowingEnum final {
    RSTD_ENUM(ThrowingEnum, (Value, (ThrowingPayload value;)))
};

static_assert(noexcept(rstd::tuple<NoThrowPayload> { rstd::declval<NoThrowPayload>() }));
static_assert(! noexcept(rstd::tuple<ThrowingPayload> { rstd::declval<ThrowingPayload>() }));

static_assert(noexcept(rstd::array<NoThrowPayload, 1> { rstd::declval<NoThrowPayload>() }));
static_assert(! noexcept(rstd::array<ThrowingPayload, 1> { rstd::declval<ThrowingPayload>() }));

static_assert(noexcept(NoThrowChoice::with<ChoiceTag::Value>(rstd::declval<NoThrowPayload>())));
static_assert(! noexcept(ThrowingChoice::with<ChoiceTag::Value>(rstd::declval<ThrowingPayload>())));

static_assert(noexcept(rstd::Result<NoThrowPayload, int> {}));
static_assert(! noexcept(rstd::Result<ThrowingPayload, int> {}));

static_assert(
    noexcept(rstd::Option<NoThrowPayload>(rstd::declval<rstd::Option<NoThrowPayload>>())));
static_assert(
    ! noexcept(rstd::Option<ThrowingPayload>(rstd::declval<rstd::Option<ThrowingPayload>>())));

static_assert(noexcept(rstd::Poll<NoThrowPayload>(rstd::declval<rstd::Poll<NoThrowPayload>>())));
static_assert(
    ! noexcept(rstd::Poll<ThrowingPayload>(rstd::declval<rstd::Poll<ThrowingPayload>>())));

static_assert(
    noexcept(rstd::mem::MaybeUninit<NoThrowPayload>::make(rstd::declval<NoThrowPayload>())));
static_assert(
    ! noexcept(rstd::mem::MaybeUninit<ThrowingPayload>::make(rstd::declval<ThrowingPayload>())));

static_assert(
    noexcept(rstd::mem::ManuallyDrop<NoThrowPayload>::make(rstd::declval<NoThrowPayload>())));
static_assert(
    ! noexcept(rstd::mem::ManuallyDrop<ThrowingPayload>::make(rstd::declval<ThrowingPayload>())));

static_assert(noexcept(NoThrowEnum::Value(rstd::declval<NoThrowPayload>())));
static_assert(! noexcept(ThrowingEnum::Value(rstd::declval<ThrowingPayload>())));

auto no_throw_contract() noexcept(
    noexcept(rstd::Result<NoThrowPayload, int> {}) &&
    noexcept(NoThrowChoice::with<ChoiceTag::Value>(rstd::declval<NoThrowPayload>())) &&
    noexcept(NoThrowEnum::Value(rstd::declval<NoThrowPayload>()))) -> int {
    return 1;
}

auto throwing_contract() noexcept(noexcept(rstd::Result<ThrowingPayload, int> {})) -> int {
    return 2;
}

} // namespace noexcept_module_check
