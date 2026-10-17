export module rstd.tests.char_core_check;
import rstd.core;

namespace
{
constexpr auto check_char_core() -> bool {
    auto value = rstd::Char::from_char32(U'中');
    if (value.is_none() || ! value->is_alphabetic()) return false;
    rstd::byte buffer[4] {};
    auto       length        = value->encode_utf8(buffer);
    auto [decoded, consumed] = rstd::char_::decode_char_utf8(buffer, length);
    return decoded == *value && consumed == rstd::usize(3);
}
static_assert(check_char_core());
} // namespace
export auto char_core_check() -> bool {
    return check_char_core();
}
