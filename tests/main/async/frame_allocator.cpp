#include <rstd/test/gtest.hpp>
#include <atomic>

import rstd;

using namespace rstd;
using namespace rstd::prelude;
using rstd::alloc::Allocator;
using rstd::alloc::Layout;
using rstd::async::FrameAllocator;
using rstd::async::FrameAllocatorOptions;
using rstd::async::FrameAllocatorSnapshot;

namespace
{

static_assert(Impled<FrameAllocator,
                     Allocator,
                     rstd::alloc::ThreadSafeAllocator,
                     rstd::alloc::AllocatorClone,
                     rstd::clone::Clone>);

auto lifetime_count(const FrameAllocatorSnapshot& snapshot) -> usize {
    usize total;
    for (usize index; index < snapshot.records.lifetime_counts.len(); ++index)
        total += snapshot.records.lifetime_counts[index];
    return total;
}

void expect_record_totals(const FrameAllocatorSnapshot& snapshot) {
    const auto& records = snapshot.records;
    usize       allocations, deallocations, live_allocations, live_bytes;
    for (usize index; index < snapshot.classes.len(); ++index) {
        const auto& bucket = snapshot.classes[index];
        allocations += bucket.allocations;
        deallocations += bucket.deallocations;
        live_allocations += bucket.live_allocations;
        live_bytes += bucket.live_bytes;
        EXPECT_EQ(bucket.allocations, bucket.deallocations + bucket.live_allocations);
        EXPECT_GE(bucket.peak_live_allocations, bucket.live_allocations);
        EXPECT_GE(bucket.peak_live_bytes, bucket.live_bytes);
    }
    EXPECT_EQ(allocations, records.allocations);
    EXPECT_EQ(deallocations, records.deallocations);
    EXPECT_EQ(live_allocations, records.live_allocations);
    EXPECT_EQ(live_bytes, records.live_bytes);
    EXPECT_EQ(records.allocations, records.deallocations + records.live_allocations);
    EXPECT_GE(records.peak_live_allocations, records.live_allocations);
    EXPECT_GE(records.peak_live_bytes, records.live_bytes);
    EXPECT_EQ(lifetime_count(snapshot), records.deallocations);
    EXPECT_EQ(snapshot.pool.live_bytes, records.live_bytes + records.record_bytes);
}

void expect_records_disabled(const FrameAllocatorSnapshot& snapshot) {
    const auto& records = snapshot.records;
    EXPECT_FALSE(records.enabled);
    EXPECT_EQ(records.allocations, usize());
    EXPECT_EQ(records.deallocations, usize());
    EXPECT_EQ(records.failures, usize());
    EXPECT_EQ(records.live_allocations, usize());
    EXPECT_EQ(records.peak_live_allocations, usize());
    EXPECT_EQ(records.requested_bytes, usize());
    EXPECT_EQ(records.live_bytes, usize());
    EXPECT_EQ(records.peak_live_bytes, usize());
    EXPECT_EQ(records.record_bytes, usize());
    EXPECT_EQ(records.remote_deallocations, usize());
    EXPECT_EQ(lifetime_count(snapshot), usize());
    for (usize index; index < snapshot.classes.len(); ++index) {
        const auto& bucket = snapshot.classes[index];
        EXPECT_EQ(bucket.allocations, usize());
        EXPECT_EQ(bucket.deallocations, usize());
        EXPECT_EQ(bucket.live_allocations, usize());
        EXPECT_EQ(bucket.peak_live_allocations, usize());
        EXPECT_EQ(bucket.live_bytes, usize());
        EXPECT_EQ(bucket.peak_live_bytes, usize());
    }
}

void expect_reclaimed(const FrameAllocator& allocator) {
    auto snapshot = allocator.snapshot();
    EXPECT_EQ(snapshot.pool.live_bytes, usize());
    EXPECT_EQ(snapshot.pool.live_block_bytes, usize());
    EXPECT_EQ(snapshot.records.live_allocations, usize());
    EXPECT_EQ(snapshot.records.live_bytes, usize());
    EXPECT_EQ(snapshot.records.record_bytes, usize());
    if (snapshot.records.enabled)
        expect_record_totals(snapshot);
    else
        expect_records_disabled(snapshot);

    allocator.trim();
    auto trimmed = allocator.snapshot();
    EXPECT_EQ(trimmed.pool.reserved_bytes, usize());
    EXPECT_EQ(trimmed.pool.retained_bytes, usize());
    EXPECT_EQ(trimmed.pool.metadata_bytes, usize());
    EXPECT_EQ(trimmed.pool.slabs, usize());
    EXPECT_EQ(trimmed.upstream.mapped_bytes, usize());
    EXPECT_EQ(trimmed.upstream.mappings, usize());
    EXPECT_EQ(trimmed.upstream.allocation_calls, trimmed.upstream.deallocation_calls);
    EXPECT_GT(trimmed.control_bytes, usize());
    EXPECT_EQ(trimmed.control_bytes, snapshot.control_bytes);
}

struct FrameDrop {
    std::atomic<int>* drops;

    explicit FrameDrop(std::atomic<int>& drops): drops(&drops) {}
    FrameDrop(const FrameDrop&) = delete;
    FrameDrop(FrameDrop&& other) noexcept: drops(rstd::exchange(other.drops, nullptr)) {}
    ~FrameDrop() {
        if (drops != nullptr) drops->fetch_add(1, std::memory_order_relaxed);
    }
};

struct PendingFuture {
    using Output = void;
    std::atomic<bool>* polled;

    auto poll(mut_ref<PendingFuture> self, task::Context&) -> task::Poll<void> {
        self->polled->store(true, std::memory_order_release);
        return task::Poll<void>::Pending();
    }
};

[[clang::noinline]]
auto completed_frame(FrameDrop token) -> async::coro<int> {
    (void)token;
    co_await async::yield_now();
    co_return 41;
}

[[clang::noinline]]
auto nested_frame(FrameDrop token) -> async::coro<int> {
    co_return co_await completed_frame(rstd::move(token));
}

[[clang::noinline]]
auto pending_frame(FrameDrop token, std::atomic<bool>& polled) -> async::coro<void> {
    (void)token;
    co_await PendingFuture { &polled };
}

TEST(FrameAllocator, DefaultFineClassesAvoidPowerOfTwoRounding) {
    auto allocator = FrameAllocator::try_make().unwrap();
    auto layout    = Layout::from_size_align(usize(50 * 1024), usize(16)).unwrap();
    auto block     = as<Allocator>(allocator).allocate(layout).unwrap();
    auto snapshot  = allocator.snapshot();
    EXPECT_EQ(FrameAllocatorOptions {}.pool.size_classes, rstd::alloc::PoolSizeClasses::Fine);
    EXPECT_EQ(block.size, layout.size());
    EXPECT_EQ(snapshot.pool.live_bytes, layout.size());
    EXPECT_LT(snapshot.pool.live_block_bytes, usize(64 * 1024));
    EXPECT_EQ(snapshot.pool.allocations, usize(1));
    expect_records_disabled(snapshot);
    as<Allocator>(allocator).deallocate(block.pointer, layout);
    expect_reclaimed(allocator);
}

TEST(FrameAllocator, ClonesKeepControlOwnerAndLiveAllocationsInEitherDropOrder) {
    const bool orders[] { false, true };
    for (bool drop_original : orders) {
        auto original = Some(FrameAllocator::try_make({ .record_allocations = true }).unwrap());
        auto cloned   = Some(as<rstd::clone::Clone>(*original).clone());
        auto initial  = original->snapshot();
        EXPECT_GT(initial.control_bytes, usize());
        EXPECT_EQ(initial.upstream.mapped_bytes, usize());
        EXPECT_EQ(initial.upstream.mappings, usize());
        EXPECT_EQ(initial.upstream.allocation_calls, usize());
        EXPECT_EQ(initial.pool.reserved_bytes, usize());
        EXPECT_EQ(cloned->snapshot().control_bytes, initial.control_bytes);
        auto layout = Layout::from_size_align(usize(257), usize(16)).unwrap();
        auto block  = original->allocate(layout).unwrap();
        rstd::mem::memset(block.pointer, u8(73), layout.size());
        EXPECT_EQ(cloned->snapshot().records.live_bytes, layout.size());
        if (drop_original)
            original = None();
        else
            cloned = None();

        auto& survivor = drop_original ? *cloned : *original;
        EXPECT_EQ(survivor.snapshot().control_bytes, initial.control_bytes);
        auto last = survivor.clone();
        if (drop_original)
            cloned = None();
        else
            original = None();
        EXPECT_EQ(static_cast<unsigned char*>(block.pointer)[256], 73u);
        EXPECT_EQ(last.snapshot().records.live_allocations, usize(1));
        as<Allocator>(last).deallocate(block.pointer, layout);
        EXPECT_EQ(last.snapshot().records.deallocations, usize(1));
        expect_reclaimed(last);
    }
}

TEST(FrameAllocator, ArbitraryReleaseAndTrimPreserveLivePayloads) {
    FrameAllocatorOptions options { .record_allocations = true };
    options.pool.slab_bytes     = usize(4096);
    options.pool.retained_bytes = usize(32768);
    auto           allocator    = FrameAllocator::try_make(options).unwrap();
    const unsigned sizes[] { 32, 256, 1024, 4096, 80, 512, 70000, 32 };
    Layout         layouts[8];
    void*          pointers[8] {};
    usize          requested;
    for (unsigned i = 0; i < 8; ++i) {
        layouts[i]  = Layout::from_size_align(usize(sizes[i]), usize(16)).unwrap();
        pointers[i] = allocator.allocate(layouts[i]).unwrap().pointer;
        rstd::mem::memset(pointers[i], u8(i + 1), layouts[i].size());
        requested += layouts[i].size();
    }
    auto peak = allocator.snapshot();
    EXPECT_EQ(peak.records.live_allocations, usize(8));
    EXPECT_EQ(peak.records.live_bytes, requested);
    EXPECT_EQ(peak.records.peak_live_bytes, requested);
    EXPECT_EQ(peak.classes[usize(64)].allocations, usize(1));
    expect_record_totals(peak);

    const unsigned released[] { 2, 0, 6, 4 };
    for (unsigned index : released) {
        allocator.deallocate(pointers[index], layouts[index]);
        pointers[index] = nullptr;
        requested -= layouts[index].size();
    }
    auto before_trim = allocator.snapshot();
    auto trimmed     = allocator.trim();
    auto after_trim  = allocator.snapshot();
    EXPECT_GT(trimmed.bytes, usize());
    EXPECT_EQ(before_trim.pool.reserved_bytes - after_trim.pool.reserved_bytes, trimmed.bytes);
    EXPECT_EQ(before_trim.pool.slabs - after_trim.pool.slabs, trimmed.slabs);
    EXPECT_EQ(after_trim.pool.retained_bytes, usize());
    EXPECT_EQ(after_trim.records.live_allocations, usize(4));
    EXPECT_EQ(after_trim.records.live_bytes, requested);
    EXPECT_EQ(after_trim.pool.live_bytes, before_trim.pool.live_bytes);
    EXPECT_EQ(after_trim.records.record_bytes, before_trim.records.record_bytes);
    expect_record_totals(after_trim);

    const unsigned survivors[] { 7, 1, 5, 3 };
    for (unsigned index : survivors) {
        auto* bytes = static_cast<unsigned char*>(pointers[index]);
        EXPECT_EQ(bytes[0], index + 1);
        EXPECT_EQ(bytes[sizes[index] - 1], index + 1);
        allocator.deallocate(pointers[index], layouts[index]);
    }
    auto released_all = allocator.snapshot();
    EXPECT_EQ(released_all.records.allocations, usize(8));
    EXPECT_EQ(released_all.records.deallocations, usize(8));
    EXPECT_EQ(released_all.records.requested_bytes, peak.records.requested_bytes);
    EXPECT_EQ(released_all.records.peak_live_allocations, usize(8));
    EXPECT_EQ(released_all.records.peak_live_bytes, peak.records.peak_live_bytes);
    EXPECT_EQ(lifetime_count(released_all), usize(8));
    expect_reclaimed(allocator);
}

TEST(FrameAllocator, RepeatedBurstsKeepEmptyCacheBoundedAndHonorTrimRetention) {
    FrameAllocatorOptions options;
    options.pool.slab_bytes     = usize(4096);
    options.pool.retained_bytes = usize(8192);
    auto allocator              = FrameAllocator::try_make(options).unwrap();
    auto layout                 = Layout::from_size_align(usize(1008), usize(16)).unwrap();
    for (unsigned burst = 0; burst < 3; ++burst) {
        void* pointers[128] {};
        for (auto& pointer : pointers) pointer = allocator.allocate(layout).unwrap().pointer;
        EXPECT_GT(allocator.snapshot().pool.reserved_bytes, options.pool.retained_bytes);
        for (unsigned i = 0; i < 128; ++i) allocator.deallocate(pointers[(i * 37) % 128], layout);
        auto idle = allocator.snapshot();
        EXPECT_EQ(idle.pool.live_bytes, usize());
        EXPECT_EQ(idle.pool.reserved_bytes, idle.pool.retained_bytes);
        EXPECT_LE(idle.pool.retained_bytes, options.pool.retained_bytes);
        expect_records_disabled(idle);
    }
    allocator.trim(usize(4096));
    EXPECT_LE(allocator.snapshot().pool.retained_bytes, usize(4096));
    expect_reclaimed(allocator);
}

TEST(FrameAllocator, ZeroSizedLayoutsDoNotAllocateOrRecordWithEitherSetting) {
    const bool modes[] { false, true };
    for (bool record : modes) {
        auto allocator = FrameAllocator::try_make({ .record_allocations = record }).unwrap();
        auto before    = allocator.snapshot();
        auto layout    = Layout::from_size_align(usize(), usize(256)).unwrap();
        auto block     = allocator.allocate(layout).unwrap();
        auto zeroed    = as<Allocator>(allocator).allocate_zeroed(layout).unwrap();
        EXPECT_EQ(block.pointer, layout.dangling());
        EXPECT_EQ(zeroed.pointer, layout.dangling());
        EXPECT_EQ(block.size, usize());
        EXPECT_EQ(zeroed.size, usize());
        allocator.deallocate(block.pointer, layout);
        as<Allocator>(allocator).deallocate(zeroed.pointer, layout);
        auto after = allocator.snapshot();
        EXPECT_EQ(after.control_bytes, before.control_bytes);
        EXPECT_EQ(after.pool.allocations, usize());
        EXPECT_EQ(after.pool.failures, usize());
        EXPECT_EQ(after.pool.slabs, usize());
        EXPECT_EQ(after.upstream.allocation_calls, before.upstream.allocation_calls);
        EXPECT_EQ(after.records.enabled, record);
        EXPECT_EQ(after.records.allocations, usize());
        EXPECT_EQ(after.records.deallocations, usize());
        EXPECT_EQ(after.records.failures, usize());
        EXPECT_EQ(after.records.requested_bytes, usize());
        if (record) expect_record_totals(after);
        expect_reclaimed(allocator);
    }
}

TEST(FrameAllocator, DisabledRecordsLeavePoolRequestAccountingWithoutDiagnosticStorage) {
    auto allocator     = FrameAllocator::try_make().unwrap();
    auto first_layout  = Layout::from_size_align(usize(17), usize(16)).unwrap();
    auto second_layout = Layout::from_size_align(usize(1001), usize(16)).unwrap();
    auto first         = allocator.allocate(first_layout).unwrap();
    auto second        = as<Allocator>(allocator).allocate_zeroed(second_layout).unwrap();
    auto snapshot      = allocator.snapshot();
    EXPECT_EQ(snapshot.pool.allocations, usize(2));
    EXPECT_EQ(snapshot.pool.live_bytes, first_layout.size() + second_layout.size());
    EXPECT_GT(snapshot.pool.metadata_bytes, usize());
    EXPECT_GE(snapshot.upstream.mapped_bytes, snapshot.pool.reserved_bytes);
    expect_records_disabled(snapshot);
    for (unsigned i = 0; i < 1001; ++i)
        EXPECT_EQ(static_cast<unsigned char*>(second.pointer)[i], 0u);
    allocator.deallocate(first.pointer, first_layout);
    allocator.deallocate(second.pointer, second_layout);
    expect_reclaimed(allocator);
}

TEST(FrameAllocator, RecordedRequestsSeparateActiveMetadataAndUseActualPoolInputClass) {
    FrameAllocatorOptions options { .record_allocations = true };
    auto                  allocator    = FrameAllocator::try_make(options).unwrap();
    auto                  first_layout = Layout::from_size_align(usize(16), usize(16)).unwrap();
    auto                  first        = as<Allocator>(allocator).allocate(first_layout).unwrap();
    auto                  single       = allocator.snapshot();
    EXPECT_GT(single.records.record_bytes, usize());
    EXPECT_EQ(single.records.live_bytes, first_layout.size());
    EXPECT_GT(single.pool.live_bytes, single.records.live_bytes);
    expect_record_totals(single);

    rstd::alloc::PoolAllocator<> classifier(options.pool);
    auto input   = Layout::from_size_align(single.pool.live_bytes, first_layout.align()).unwrap();
    auto actual  = classifier.classify(input).unwrap();
    auto logical = classifier.classify(first_layout).unwrap();
    EXPECT_NE(actual.index, logical.index);
    EXPECT_EQ(single.classes[actual.index].capacity, actual.capacity);
    EXPECT_EQ(single.classes[actual.index].allocations, usize(1));
    EXPECT_EQ(single.classes[logical.index].allocations, usize());

    auto second_layout = Layout::from_size_align(usize(1001), usize(16)).unwrap();
    auto second        = allocator.allocate_zeroed(second_layout).unwrap();
    auto live          = allocator.snapshot();
    auto requested     = first_layout.size() + second_layout.size();
    EXPECT_TRUE(live.records.enabled);
    EXPECT_EQ(live.records.allocations, usize(2));
    EXPECT_EQ(live.records.live_allocations, usize(2));
    EXPECT_EQ(live.records.peak_live_allocations, usize(2));
    EXPECT_EQ(live.records.requested_bytes, requested);
    EXPECT_EQ(live.records.live_bytes, requested);
    EXPECT_EQ(live.records.peak_live_bytes, requested);
    EXPECT_GT(live.records.record_bytes, single.records.record_bytes);
    EXPECT_GT(live.pool.metadata_bytes, usize());
    EXPECT_EQ(lifetime_count(live), usize());
    expect_record_totals(live);
    for (unsigned i = 0; i < 1001; ++i)
        EXPECT_EQ(static_cast<unsigned char*>(second.pointer)[i], 0u);

    allocator.deallocate(first.pointer, first_layout);
    auto remaining = allocator.snapshot();
    EXPECT_EQ(remaining.records.live_bytes, second_layout.size());
    EXPECT_EQ(remaining.records.requested_bytes, requested);
    EXPECT_GT(remaining.records.record_bytes, usize());
    EXPECT_LT(remaining.records.record_bytes, live.records.record_bytes);
    EXPECT_EQ(lifetime_count(remaining), usize(1));
    expect_record_totals(remaining);
    allocator.deallocate(second.pointer, second_layout);
    EXPECT_EQ(allocator.snapshot().records.remote_deallocations, usize());
    EXPECT_EQ(lifetime_count(allocator.snapshot()), usize(2));
    expect_reclaimed(allocator);
}

TEST(FrameAllocator, OverAlignedDedicatedAllocationsPreserveAlignmentAndZeroing) {
    const bool modes[] { false, true };
    for (bool record : modes) {
        auto allocator = FrameAllocator::try_make({ .record_allocations = record }).unwrap();
        auto layout    = Layout::from_size_align(usize(129), usize(64)).unwrap();
        auto block     = as<Allocator>(allocator).allocate_zeroed(layout).unwrap();
        EXPECT_EQ(reinterpret_cast<uintptr_t>(block.pointer) % 64, 0u);
        EXPECT_EQ(block.size, layout.size());
        for (unsigned i = 0; i < 129; ++i)
            EXPECT_EQ(static_cast<unsigned char*>(block.pointer)[i], 0u);
        auto live = allocator.snapshot();
        if (record) {
            EXPECT_EQ(live.classes[usize(64)].allocations, usize(1));
            EXPECT_EQ(live.classes[usize(64)].live_bytes, layout.size());
            expect_record_totals(live);
        } else {
            EXPECT_EQ(live.pool.live_bytes, layout.size());
            expect_records_disabled(live);
        }
        allocator.deallocate(block.pointer, layout);
        EXPECT_EQ(allocator.snapshot().pool.reserved_bytes, usize());
        expect_reclaimed(allocator);
    }
}

TEST(FrameAllocator, UnsupportedAlignmentAndOverflowFailWithoutDamagingLiveBlocks) {
    const bool modes[] { false, true };
    for (bool record : modes) {
        auto allocator = FrameAllocator::try_make({ .record_allocations = record }).unwrap();
        auto layout    = Layout::from_size_align(usize(48), usize(16)).unwrap();
        auto block     = allocator.allocate(layout).unwrap();
        rstd::mem::memset(block.pointer, u8(91), layout.size());
        auto                                before = allocator.snapshot();
        rstd::alloc::VirtualMemoryAllocator source;
        auto                                unsupported =
            Layout::from_size_align(usize(64), source.mapping_alignment() * usize(2)).unwrap();
        auto overflow =
            Layout::from_size_align(usize(isize::MAX.to_primitive()), usize(1)).unwrap();
        EXPECT_TRUE(allocator.allocate(unsupported).is_err());
        EXPECT_TRUE(allocator.allocate(overflow).is_err());
        EXPECT_TRUE(as<Allocator>(allocator).allocate_zeroed(overflow).is_err());
        auto after = allocator.snapshot();
        EXPECT_EQ(after.pool.live_bytes, before.pool.live_bytes);
        EXPECT_EQ(after.pool.reserved_bytes, before.pool.reserved_bytes);
        EXPECT_EQ(after.upstream.mapped_bytes, before.upstream.mapped_bytes);
        EXPECT_EQ(after.records.live_bytes, before.records.live_bytes);
        EXPECT_EQ(after.records.record_bytes, before.records.record_bytes);
        EXPECT_EQ(after.records.allocations, before.records.allocations);
        EXPECT_EQ(after.records.requested_bytes, before.records.requested_bytes);
        if (record) {
            EXPECT_EQ(after.records.failures, usize(3));
            expect_record_totals(after);
        } else {
            expect_records_disabled(after);
        }
        for (unsigned i = 0; i < 48; ++i)
            EXPECT_EQ(static_cast<unsigned char*>(block.pointer)[i], 91u);
        allocator.deallocate(block.pointer, layout);
        // Rejected upstream calls need not have a matching deallocation call.
        allocator.trim();
        auto released = allocator.snapshot();
        EXPECT_EQ(released.upstream.mapped_bytes, usize());
        EXPECT_EQ(released.upstream.mappings, usize());
        EXPECT_EQ(released.pool.reserved_bytes, usize());
        EXPECT_EQ(released.records.record_bytes, usize());
        if (record) expect_record_totals(released);
    }
}

TEST(FrameAllocator, DirectCrossThreadReleaseUpdatesSharedRecordsWithEitherSetting) {
    const bool modes[] { false, true };
    for (bool record : modes) {
        auto   allocator = FrameAllocator::try_make({ .record_allocations = record }).unwrap();
        Layout layouts[] {
            Layout::from_size_align(usize(33), usize(16)).unwrap(),
            Layout::from_size_align(usize(1200), usize(16)).unwrap(),
            Layout::from_size_align(usize(70000), usize(16)).unwrap(),
        };
        void* pointers[3] {};
        for (unsigned i = 0; i < 3; ++i) {
            pointers[i] = allocator.allocate(layouts[i]).unwrap().pointer;
            rstd::mem::memset(pointers[i], u8(i + 1), layouts[i].size());
        }
        auto worker = thread::spawn([owner = allocator.clone(), &layouts, &pointers]() mutable {
                          bool           intact = true;
                          const unsigned order[] { 1, 0, 2 };
                          for (unsigned i : order) {
                              auto* bytes = static_cast<unsigned char*>(pointers[i]);
                              intact      = intact && bytes[0] == i + 1 &&
                                            bytes[layouts[i].size().to_primitive() - 1] == i + 1;
                              owner.deallocate(pointers[i], layouts[i]);
                          }
                          return intact;
                      }).unwrap();
        EXPECT_TRUE(rstd::move(worker).join().unwrap());
        auto released = allocator.snapshot();
        EXPECT_EQ(released.pool.allocations, usize(3));
        if (record) {
            EXPECT_EQ(released.records.allocations, usize(3));
            EXPECT_EQ(released.records.deallocations, usize(3));
            EXPECT_EQ(released.records.remote_deallocations, usize(3));
            EXPECT_EQ(lifetime_count(released), usize(3));
            expect_record_totals(released);
        } else {
            expect_records_disabled(released);
        }
        expect_reclaimed(allocator);
    }
}

TEST(FrameAllocator, RuntimeFactoriesCompleteAndReleaseFramesWithEitherSetting) {
    const bool modes[] { false, true };
    for (bool record : modes) {
        auto allocator = FrameAllocator::try_make({ .record_allocations = record }).unwrap();
        std::atomic<int> drops { 0 };
        {
            auto builder = async::RuntimeBuilder::current_thread();
            builder.frame_allocator(::alloc::sync::SharedAllocator::make(allocator.clone()));
            auto runtime = builder.build().unwrap();
            EXPECT_EQ(runtime.block_on_with([&] {
                return nested_frame(FrameDrop { drops });
            }),
                      41);
            auto joined = runtime.spawn_with([&] {
                return nested_frame(FrameDrop { drops });
            });
            auto result = runtime.block_on_with([task = rstd::move(joined)]() mutable {
                return rstd::move(task);
            });
            ASSERT_TRUE(result.is_ok());
            EXPECT_EQ(result.unwrap_unchecked(), 41);
        }
        EXPECT_EQ(drops.load(std::memory_order_acquire), 2);
        auto released = allocator.snapshot();
        EXPECT_GT(released.pool.allocations, usize());
        if (record) {
            EXPECT_GT(released.records.allocations, usize());
            EXPECT_EQ(released.records.allocations, released.records.deallocations);
            expect_record_totals(released);
        } else {
            expect_records_disabled(released);
        }
        expect_reclaimed(allocator);
    }
}

TEST(FrameAllocator, RuntimeAbortReleasesPendingFramesAfterCancellation) {
    auto              allocator = FrameAllocator::try_make({ .record_allocations = true }).unwrap();
    std::atomic<int>  drops { 0 };
    std::atomic<bool> polled { false };
    {
        auto builder = async::RuntimeBuilder::multi_thread();
        builder.worker_threads(usize(1)).frame_allocator(
            ::alloc::sync::SharedAllocator::make(allocator.clone()));
        auto runtime = builder.build().unwrap();
        auto joined  = runtime.spawn_with([&] {
            return pending_frame(FrameDrop { drops }, polled);
        });
        for (int i = 0; i < 2000 && ! polled.load(std::memory_order_acquire); ++i)
            thread::sleep(time::Duration::from_millis(u64(1)));
        EXPECT_TRUE(polled.load(std::memory_order_acquire));
        EXPECT_EQ(drops.load(std::memory_order_acquire), 0);
        EXPECT_GT(allocator.snapshot().records.live_allocations, usize());
        joined.abort();
        auto result = runtime.block_on_with([task = rstd::move(joined)]() mutable {
            return rstd::move(task);
        });
        ASSERT_TRUE(result.is_err());
        EXPECT_TRUE(rstd::move(result).unwrap_err().is_aborted());
    }
    EXPECT_EQ(drops.load(std::memory_order_acquire), 1);
    auto released = allocator.snapshot();
    EXPECT_GT(released.records.allocations, usize());
    EXPECT_EQ(lifetime_count(released), released.records.allocations);
    expect_reclaimed(allocator);
}

TEST(FrameAllocator, RuntimeShutdownReleasesUnstartedFactoryFrames) {
    auto              allocator = FrameAllocator::try_make({ .record_allocations = true }).unwrap();
    std::atomic<int>  drops { 0 };
    std::atomic<bool> polled { false };
    auto              joined = [&] {
        auto builder = async::RuntimeBuilder::current_thread();
        builder.frame_allocator(::alloc::sync::SharedAllocator::make(allocator.clone()));
        auto runtime = builder.build().unwrap();
        return runtime.spawn_with([&] {
            return pending_frame(FrameDrop { drops }, polled);
        });
    }();
    EXPECT_FALSE(polled.load(std::memory_order_acquire));
    EXPECT_EQ(drops.load(std::memory_order_acquire), 1);
    EXPECT_TRUE(joined.is_finished());
    auto result = async::block_on(rstd::move(joined));
    ASSERT_TRUE(result.is_err());
    EXPECT_TRUE(rstd::move(result).unwrap_err().is_aborted());
    auto released = allocator.snapshot();
    EXPECT_GT(released.records.allocations, usize());
    EXPECT_EQ(lifetime_count(released), released.records.allocations);
    expect_reclaimed(allocator);
}

} // namespace
