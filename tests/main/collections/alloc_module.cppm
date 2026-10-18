export module rstd.tests.alloc_collections_module;
import rstd.alloc;

using namespace rstd::prelude;
export auto alloc_collections_reserve() -> bool {
    auto storage = ::alloc::boxed::Box<rstd::byte[]>::make_uninit(usize(4));
    if (storage.as_ptr().len() != usize(4)) return false;
    using Error = ::alloc::collections::TryReserveError;
    ::alloc::vec::Vec<u64>                  bytes;
    ::alloc::collections::HashMap<i32, i32> map;
    ::alloc::collections::HashSet<i32>      set;
    return bytes.try_reserve(usize::MAX).unwrap_err() == Error::CapacityOverflow &&
           map.try_reserve(usize::MAX).unwrap_err() == Error::CapacityOverflow &&
           set.try_reserve(usize::MAX).unwrap_err() == Error::CapacityOverflow &&
           map.try_reserve(usize(3)).is_ok() && set.try_reserve(usize(3)).is_ok();
}
