using namespace rstd::literals;
static void pool_lifetime() {
    using namespace rstd::alloc;
    auto pool   = SharedPoolAllocator<VirtualMemoryAllocator>::try_make_in(
                      VirtualMemoryAllocator {},
                      PoolOptions { .retained_bytes = usize(262144) },
                      VirtualMemoryAllocator {})
                      .unwrap();
    auto erased = SharedAllocator::try_make_in(VirtualMemoryAllocator {}, pool.clone()).unwrap();
    auto first = rstd::bytes::BytesMut::try_with_capacity_in(usize(16384), erased.clone()).unwrap();
    first.extend_from_slice("hello"_str.as_bytes());
    auto frozen = first.freeze();
    auto shared = frozen.clone();
    rstd_assert(rstd::move(frozen).try_into_mut().is_err());
    rstd_assert(pool.trim().slabs == usize());
    auto recovered = rstd::move(shared).try_into_mut().unwrap();
    rstd_assert(recovered.as_slice() == "hello"_str.as_bytes());
    recovered = {};
    rstd_assert(pool.statistics().live_bytes == usize());
    pool.trim();
    rstd_assert(pool.upstream_statistics().mapped_bytes == usize());
    auto zero  = Layout::from_size_align(usize(), usize(16)).unwrap();
    auto empty = pool.allocate(zero).unwrap();
    pool.deallocate(empty.pointer, zero);
    rstd_assert(pool.statistics().live_bytes == usize());
    const unsigned sizes[] { 1, 15, 16, 17, 31, 32, 33, 4095, 4096, 4097, 65535, 65536, 65537 };
    for (auto size : sizes) {
        auto boundary = Layout::from_size_align(usize(size), usize(16)).unwrap();
        auto block    = pool.allocate(boundary).unwrap();
        rstd_assert(reinterpret_cast<rstd::uintptr_t>(block.pointer) % 16 == 0);
        static_cast<unsigned char*>(block.pointer)[size - 1] = 42;
        pool.deallocate(block.pointer, boundary);
    }
    auto layout = Layout::from_size_align(usize(33), usize(16)).unwrap();
    auto api    = rstd::as<Allocator>(pool);
    auto block  = api.allocate(layout).unwrap();
    rstd::mem::memset(block.pointer, u8(0xff), layout.size);
    auto address = block.pointer;
    api.deallocate(block.pointer, layout);
    block = api.allocate_zeroed(layout).unwrap();
    rstd_assert(block.pointer == address);
    for (unsigned i = 0; i < 33; ++i)
        rstd_assert(static_cast<unsigned char*>(block.pointer)[i] == 0);
    api.deallocate(block.pointer, layout);
    pool.trim();
    auto small        = Layout::from_size_align(usize(64), usize(16)).unwrap();
    auto grown_layout = Layout::from_size_align(usize(128), usize(16)).unwrap();
    auto original     = api.allocate(small).unwrap();
    rstd::mem::memset(original.pointer, u8(42), small.size);
    auto grown = api.grow_zeroed(original.pointer, small, grown_layout).unwrap();
    for (unsigned i = 0; i < 128; ++i)
        rstd_assert(static_cast<unsigned char*>(grown.pointer)[i] == (i < 64 ? 42 : 0));
    api.deallocate(grown.pointer, grown_layout);
    pool.trim();
    auto large      = Layout::from_size_align(usize(70000), usize(64)).unwrap();
    auto allocation = api.allocate(large).unwrap();
    rstd_assert(reinterpret_cast<rstd::uintptr_t>(allocation.pointer) % 64 == 0);
    api.deallocate(allocation.pointer, large);
    rstd_assert(pool.upstream_statistics().mapped_bytes == usize());
    rstd_assert(api.allocate(Layout::from_size_align_unchecked(usize::MAX, usize(16))).is_err());
    auto typed =
        ::alloc::vec::Vec<u64, SharedPoolAllocator<VirtualMemoryAllocator>>::make_in(pool.clone());
    typed.reserve_exact(usize(4));
    typed.push(u64(42));
    auto aligned = rstd::bytes::BytesMut::from_vec(rstd::move(typed));
    aligned.reserve(usize(128));
    rstd_assert(reinterpret_cast<rstd::uintptr_t>(aligned.data()) % alignof(u64) == 0);
    VirtualMemoryAllocator source;
    auto page   = Layout::from_size_align(source.page_size() + usize(1), usize(16)).unwrap();
    auto mapped = source.allocate_zeroed(page).unwrap();
    rstd_assert(source.statistics().mapped_bytes == source.page_size() * usize(2));
    VirtualMemoryAllocator moved(rstd::move(source));
    rstd_assert(source.statistics().mapped_bytes == usize());
    rstd_assert(moved.statistics().mappings == usize(1));
    moved.deallocate(mapped.pointer, page);
    rstd_assert(moved.statistics().mapped_bytes == usize());
    rstd_assert(moved.statistics().released_bytes == moved.page_size() * usize(2));
}

static void pool_failures() {
    using namespace rstd::alloc;
    auto control = BudgetAllocator<>::try_make(usize(1)).unwrap();
    rstd_assert(SharedPoolAllocator<VirtualMemoryAllocator>::try_make_in(control.clone()).is_err());
    rstd_assert(control.statistics().live_bytes == usize());
    auto tight = BudgetAllocator<>::try_make(usize(4096)).unwrap();
    auto bytes = rstd::bytes::BytesMut::try_with_capacity_in(usize(65536), tight.clone());
    rstd_assert(bytes.is_err() && tight.statistics().live_bytes == usize());
    rstd_assert(tight.statistics().allocations == usize(1));
    auto none = BudgetAllocator<>::try_make(usize(1)).unwrap();
    rstd_assert(rstd::bytes::BytesMut::try_with_capacity_in(usize(64), none.clone()).is_err());
    rstd_assert(none.statistics().live_bytes == usize());
    auto owner_budget = BudgetAllocator<>::try_make(usize(1048576)).unwrap();
    {
        auto weak = [&] {
            auto pool =
                SharedPoolAllocator<BudgetAllocator<>>::try_make({}, owner_budget.clone()).unwrap();
            auto value = ::alloc::sync::Arc<usize>::try_make_in(pool.clone(), usize(42)).unwrap();
            return value.downgrade();
        }();
        rstd_assert(weak.expired());
        rstd_assert(owner_budget.statistics().live_bytes != usize());
    }
    rstd_assert(owner_budget.statistics().live_bytes == usize());
    auto upstream = BudgetAllocator<>::try_make(usize(262144)).unwrap();
    auto pool     = SharedPoolAllocator<BudgetAllocator<>>::try_make({}, upstream.clone()).unwrap();
    auto small    = Layout::from_size_align(usize(64), usize(16)).unwrap();
    auto large    = Layout::from_size_align(usize(524288), usize(16)).unwrap();
    auto api      = rstd::as<Allocator>(pool);
    auto allocation                                    = api.allocate(small).unwrap();
    static_cast<unsigned char*>(allocation.pointer)[0] = 42;
    rstd_assert(api.grow(allocation.pointer, small, large).is_err());
    rstd_assert(pool.statistics().grows == usize(1));
    rstd_assert(static_cast<unsigned char*>(allocation.pointer)[0] == 42);
    api.deallocate(allocation.pointer, small);
    pool.trim();
    rstd_assert(upstream.statistics().live_bytes == usize());
}
static void pool_threads() {
    using namespace rstd::alloc;
    auto pool    = SharedPoolAllocator<VirtualMemoryAllocator>::try_make_in(
                       VirtualMemoryAllocator {},
                       PoolOptions { .retained_bytes = usize() },
                       VirtualMemoryAllocator {})
                       .unwrap();
    auto layout  = Layout::from_size_align(usize(128), usize(16)).unwrap();
    auto pointer = pool.allocate(layout).unwrap().pointer;
    auto worker  = rstd::thread::builder::Builder::make()
                       .spawn([owner = pool.clone(), pointer, layout] {
                          owner.deallocate(pointer, layout);
                          for (int i = 0; i < 50; ++i) {
                              auto value = owner.allocate(layout).unwrap();
                              owner.deallocate(value.pointer, layout);
                          }
                       })
                       .unwrap();
    for (int i = 0; i < 50; ++i) {
        auto value = pool.allocate(layout).unwrap();
        pool.deallocate(value.pointer, layout);
    }
    rstd::move(worker).join().unwrap();
    rstd_assert(pool.statistics().live_bytes == usize());
    rstd_assert(pool.upstream_statistics().mapped_bytes == usize());
}
