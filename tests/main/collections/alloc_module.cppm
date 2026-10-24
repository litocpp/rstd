export module rstd.tests.alloc_collections_module;
import rstd.alloc;

using namespace rstd::prelude;
export auto alloc_collections_reserve() -> bool {
    auto storage = ::alloc::boxed::Box<rstd::byte[]>::make_uninit(usize(4));
    if (storage.as_ptr().len() != usize(4)) return false;
    ::alloc::collections::HashTable<u32> ids;
    auto                                 entry = ids.try_entry(
                                                        u64(42),
                                                        [](u32 id) {
                            return id == u32(42);
                                                        },
                                                        [](u32 id) noexcept {
                            return u64(id.to_primitive());
                                                        })
                                                     .unwrap();
    rstd::move(entry.as_Vacant().value).insert(u32(42));
    if (**ids.find(u64(42), [](u32 id) {
            return id == u32(42);
        }) != u32(42))
        return false;
    using Error = ::alloc::collections::TryReserveError;
    ::alloc::vec::Vec<u64>                  bytes;
    ::alloc::collections::HashMap<i32, i32> map;
    ::alloc::collections::HashSet<i32>      set;
    return bytes.try_reserve(usize::MAX).unwrap_err() == Error::CapacityOverflow &&
           map.try_reserve(usize::MAX).unwrap_err() == Error::CapacityOverflow &&
           set.try_reserve(usize::MAX).unwrap_err() == Error::CapacityOverflow &&
           map.try_reserve(usize(3)).is_ok() && set.try_reserve(usize(3)).is_ok();
}
