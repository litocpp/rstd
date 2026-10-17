export module rstd.tests.char_alloc_check;
import rstd.alloc;

export auto char_alloc_check() -> bool {
    auto text = alloc::string::String::make();
    text.push(rstd::Char::from_char32(U'中').unwrap());
    auto copy = text.as_str().chars().collect<alloc::string::String>();
    return copy.as_str() == text.as_str();
}
