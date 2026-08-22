export module rstd.tests.source_location_module_check;

import rstd.basic;

export namespace source_location_module_check
{

constexpr auto capture(rstd::source_location location = rstd::source_location::current()) noexcept
    -> rstd::source_location {
    return location;
}

template<typename T>
constexpr auto
capture_template(rstd::source_location location = rstd::source_location::current()) noexcept
    -> rstd::source_location {
    return location;
}

constexpr auto capture_panic(rstd::panic_::SrcLoc location = {}) noexcept
    -> rstd::panic_::Location {
    return rstd::panic_::Location::from(location.val);
}

#line 700 "rstd-source-location-remapped.cpp"
inline constexpr auto remapped = rstd::source_location::current();

} // namespace source_location_module_check
