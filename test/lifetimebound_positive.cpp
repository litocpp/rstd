#include <fcntl.h>
import rstd;

using namespace rstd::literals;

int main() {
    auto box        = rstd::boxed::Box<int>::make(42);
    auto box_borrow = box.as_ref();

    auto owned_fd  = rstd::os::fd::OwnedFd::from_raw_fd(::open("/dev/null", O_RDONLY));
    auto fd_borrow = owned_fd.as_fd();

    auto vec = rstd::vec::Vec<int>::make();
    vec.push(7);
    auto vec_borrow = vec.as_slice();

    auto text        = rstd::string::String::make("rstd"_str);
    auto text_borrow = text.as_str();

    rstd::array<int, 2> values { 1, 2 };
    auto                array_borrow = values.as_slice();

    int  referent  = 9;
    auto projected = rstd::ref<int>::from_raw_parts(&referent).as_ptr();

    auto source_iter =
        rstd::iter::from_slice(rstd::slice<int>::from_raw_parts(&referent, rstd::usize(1)));
    auto source_item = source_iter.next();

    auto text_values = "ok"_bytes;
    auto raw_bytes   = rstd::str_::as_bytes(rstd::from_utf8_unchecked(text_values));

    auto map = rstd::collections::BTreeMap<int, int>::make();
    map.insert(1, 11);
    auto keys = decltype(map.keys())(map.iter());
    auto key  = keys.next();

    return *box_borrow == 42 && vec_borrow[rstd::usize()] == 7 && text_borrow == "rstd"_str &&
                   array_borrow[rstd::usize(1)] == 2 && *projected == 9 && **source_item == 9 &&
                   raw_bytes[rstd::usize(1)] == rstd::u8('k') && **key == 1 &&
                   fd_borrow.as_raw_fd() >= 0
               ? 0
               : 1;
}
