import rstd;

int main() {
    auto box        = rstd::boxed::Box<int>::make(42);
    auto box_borrow = box.as_ref();

    auto vec = rstd::vec::Vec<int>::make();
    vec.push(7);
    auto vec_borrow = vec.as_slice();

    auto text        = rstd::string::String::make("rstd");
    auto text_borrow = text.as_str();

    rstd::array<int, 2> values { 1, 2 };
    auto                array_borrow = values.as_slice();

    int  referent = 9;
    auto projected = rstd::ref<int>::from_raw_parts(&referent).as_ptr();

    auto source_iter = rstd::iter::from_slice(
        rstd::slice<int>::from_raw_parts(&referent, 1));
    auto source_item = source_iter.next();

    const rstd::u8 raw_text[] = { 'o', 'k' };
    auto raw_bytes = rstd::str_::as_bytes(
        rstd::ref<rstd::str>::from_raw_parts(raw_text, 2));

    auto map = rstd::collections::BTreeMap<int, int>::make();
    map.insert(1, 11);
    auto keys = decltype(map.keys())(map.iter());
    auto key  = keys.next();

    return *box_borrow == 42 && vec_borrow[0] == 7 && text_borrow == "rstd" &&
                   array_borrow[1] == 2 && *projected == 9 && **source_item == 9 &&
                   raw_bytes[1] == 'k' && **key == 1
               ? 0
               : 1;
}
