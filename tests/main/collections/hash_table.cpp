#include <rstd/test/gtest.hpp>
#if ! defined(NDEBUG) && (defined(__unix__) || defined(__APPLE__))
#include <sys/wait.h>
#include <unistd.h>
#endif
import rstd;
using namespace rstd::prelude;
using rstd::collections::HashTable;

TEST(HashTable, ExternalIdsSurviveGrowthCollisionsAndTombstones) {
    Vec<u64>       values;
    HashTable<u32> table;
    auto           hash = [&](u32 id) noexcept {
        return values[usize(id.to_primitive())] % u64(3);
    };
    for (usize i; i < usize(300); ++i) {
        values.push(u64(i.to_primitive()));
        auto entry = table
                         .try_entry(
                             u64(i.to_primitive() % 3),
                             [&](u32 id) {
                                 return values[usize(id.to_primitive())] == u64(i.to_primitive());
                             },
                             hash)
                         .unwrap();
        ASSERT_TRUE(entry.is_Vacant());
        rstd::move(entry.as_Vacant().value).insert(u32(i.to_primitive()));
    }
    for (usize i; i < usize(300); ++i) {
        auto found = table.find_entry(u64(i.to_primitive() % 3), [&](u32 id) {
            return values[usize(id.to_primitive())] == u64(i.to_primitive());
        });
        ASSERT_TRUE(found.is_some());
        if (i % usize(2) == usize()) EXPECT_EQ(rstd::move(*found).remove(), u32(i.to_primitive()));
    }
    table.try_reserve(usize(100), hash).unwrap();
    table.try_shrink_to(usize(), hash).unwrap();
    EXPECT_EQ(table.len(), usize(150));
    for (usize i; i < usize(300); ++i)
        EXPECT_EQ(table
                      .find(u64(i.to_primitive() % 3),
                            [&](u32 id) {
                                return values[usize(id.to_primitive())] == u64(i.to_primitive());
                            })
                      .is_some(),
                  i % usize(2) != usize());
    auto  iter = table.iter_mut();
    usize count;
    while (auto item = iter.next()) {
        EXPECT_TRUE(**item < u32(300));
        ++count;
    }
    EXPECT_EQ(count, table.len());
}
TEST(HashTable, BorrowedEntryPreparationDoesNotAllocateForAnExistingIdentity) {
    auto tracker = rstd::alloc::SharedTrackingAllocator<>::make();
    using Table  = HashTable<u32, decltype(tracker)>;
    {
        auto table = Table::new_in(tracker.clone());
        auto hash  = [](u32 value) noexcept {
            return u64(value.to_primitive());
        };
        table.try_reserve(usize(), hash).unwrap();
        EXPECT_EQ(table.allocation_size(), usize());
        for (int i = 0; i < 7; ++i) {
            auto entry = table.entry(
                u64(i),
                [=](u32 value) {
                    return value == u32(i);
                },
                hash);
            rstd::move(entry.as_Vacant().value).insert(u32(i));
        }
        auto before   = tracker.snapshot();
        auto occupied = table
                            .try_entry(
                                u64(3),
                                [](u32 value) {
                                    return value == u32(3);
                                },
                                hash)
                            .unwrap();
        EXPECT_TRUE(occupied.is_Occupied());
        EXPECT_EQ(tracker.snapshot().allocations, before.allocations);
        EXPECT_EQ(u64(table.allocation_size().to_primitive()), before.live_bytes);
        // Equal fingerprints are candidates, not identities.
        EXPECT_TRUE(table
                        .find(u64(4),
                              [](u32) {
                                  return false;
                              })
                        .is_none());
        {
            auto unused = table.entry(
                u64(99),
                [](u32) {
                    return false;
                },
                hash);
        }
        EXPECT_EQ(table.len(), usize(7));
        auto existing = table.find_mut(u64(3), [](u32 value) {
            return value == u32(3);
        });
        EXPECT_EQ(**existing, u32(3));
        table.clear();
        table.try_shrink_to(usize(), hash).unwrap();
        EXPECT_EQ(table.allocation_size(), usize());
    }
    EXPECT_EQ(tracker.snapshot().live_bytes, u64());
}
struct alignas(128) TableValue {
    static inline int live {};
    u32               id;
    explicit TableValue(u32 id): id(id) { ++live; }
    TableValue(const TableValue&) = delete;
    TableValue(TableValue&& other) noexcept: id(other.id) { ++live; }
    ~TableValue() { --live; }
};
TEST(HashTable, MoveOnlyAlignedElementsAndPartialOwningIterationReleaseExactlyOnce) {
    EXPECT_EQ(TableValue::live, 0);
    {
        HashTable<TableValue> table;
        auto                  hash = [](const TableValue& value) noexcept {
            return u64(value.id.to_primitive());
        };
        for (int i = 0; i < 32; ++i) {
            auto entry = table.entry(
                u64(i),
                [=](const TableValue& value) {
                    return value.id == u32(i);
                },
                hash);
            auto stored = rstd::move(entry.as_Vacant().value).insert(TableValue(u32(i)));
            EXPECT_EQ(reinterpret_cast<rstd::size_t>(&stored.get()) % alignof(TableValue),
                      rstd::size_t(0));
        }
        auto moved = rstd::move(table);
        EXPECT_TRUE(table.is_empty());
        auto iter = rstd::move(moved).into_iter();
        {
            auto first = iter.next();
            EXPECT_TRUE(first.is_some());
        }
        EXPECT_EQ(iter.len(), usize(31));
    }
    EXPECT_EQ(TableValue::live, 0);
}

TEST(HashTable, CallbacksCanBorrowMoveOnlyState) {
    HashTable<u32> table;
    auto           hash = [owned = Box<u64>::make(u64(9))](u32 value) noexcept {
        return *owned + u64(value.to_primitive());
    };
    auto equal = [owned = Box<u32>::make(u32(3))](u32 value) {
        return value == *owned;
    };
    auto entry = table.try_entry(u64(12), rstd::move(equal), hash).unwrap();
    rstd::move(entry.as_Vacant().value).insert(u32(3));
    table.reserve(usize(100), hash);
    EXPECT_TRUE(table
                    .find(u64(12),
                          [owned = Box<u32>::make(u32(3))](u32 value) {
                              return value == *owned;
                          })
                    .is_some());
}
#if ! defined(NDEBUG) && (defined(__unix__) || defined(__APPLE__))
TEST(HashTable, StructuralMutationInvalidatesEntriesAndIteratorsInDebug) {
    for (int kind = 0; kind < 3; ++kind) {
        auto child = fork();
        ASSERT_TRUE(child >= 0);
        if (child == 0) {
            close(2);
            HashTable<u32> table;
            auto           entry = table.entry(
                u64(1),
                [](u32) {
                    return false;
                },
                [](u32) noexcept {
                    return u64(1);
                });
            if (kind == 0) {
                table.clear();
                rstd::move(entry.as_Vacant().value).insert(u32(1));
            } else {
                auto occupied = rstd::move(entry.as_Vacant().value).insert(u32(1));
                auto iterator = table.iter();
                table.clear();
                if (kind == 1)
                    (void)occupied.get();
                else
                    (void)iterator.next();
            }
            _exit(0);
        }
        int status = 0;
        ASSERT_EQ(waitpid(child, &status, 0), child);
        EXPECT_TRUE(WIFSIGNALED(status));
    }
}
#endif
