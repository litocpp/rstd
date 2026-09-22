#include <rstd/test/gtest.hpp>
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;
namespace iter = rstd::iter;

struct RawReferenceIterator {
    using Item = int&;
    auto next() -> Option<Item>;
};
static_assert(! iter::has_next<RawReferenceIterator>);
static_assert(! iter::into_iterable<RawReferenceIterator>);
static_assert(! rstd::Impled<RawReferenceIterator, iter::Iterator>);
static_assert(! iter::valid_item<int&>);
static_assert(! iter::valid_item<const int&>);
static_assert(! iter::valid_item<int&&>);
static_assert(! iter::into_iterable<Option<int&>>);
static_assert(! iter::into_iterable<rstd::Result<int&, i32>>);

static_assert(rstd::mtp::same_as<iter::stored_item_t<u8>, u8>);
static_assert(rstd::mtp::same_as<iter::stored_item_t<ref<u8>>, ref<u8>>);
static_assert(rstd::mtp::same_as<iter::stored_item_t<mut_ref<u8>>, mut_ref<u8>>);
static_assert(rstd::mtp::same_as<iter::SliceIter<u8>::Item, ref<u8>>);
static_assert(rstd::mtp::same_as<iter::SliceIterMut<u8>::Item, mut_ref<u8>>);
static_assert(iter::storable_item<u8>);
static_assert(iter::storable_item<ref<u8>>);
static_assert(iter::storable_item<mut_ref<u8>>);
static_assert(! iter::storable_item<int&>);
static_assert(! iter::storable_item<const int&>);
static_assert(! iter::storable_item<u8&>);
static_assert(! iter::storable_item<const u8&>);

template<typename T>
concept CanStoreItem = requires(T&& value) { iter::store_item<T>(rstd::forward<T>(value)); };
static_assert(! CanStoreItem<u8&>);
static_assert(! CanStoreItem<const u8&>);

constexpr bool byte_iteration() {
    rstd::byte data[] = { rstd::byte { 1 }, rstd::byte { 2 }, rstd::byte { 3 } };
    auto       source = iter::from_slice(slice<u8>::from_raw_parts(data, 3_usize));
    if (source.next()->get() != u8(1) || source.next_back()->get() != u8(3)) return false;
    if (source.next()->get() != u8(2) || source.next().is_some()) return false;
    if (source.next_back().is_some()) return false;
    auto edits = iter::into_iter(mut_ref<u8[]>::from_raw_parts(data, 3_usize));
    auto first = edits.next().unwrap();
    first      = u8(9);
    if (data[0] != rstd::byte { 9 } || data[1] != rstd::byte { 2 }) return false;
    auto bytes = mut_ref<u8[]>::from_raw_parts(data, 3_usize);
    rstd::slice_::swap(bytes, 0_usize, 2_usize);
    rstd::slice_::swap(bytes, 1_usize, 1_usize);
    if (data[0] != rstd::byte { 3 } || data[1] != rstd::byte { 2 } || data[2] != rstd::byte { 9 })
        return false;
    auto empty = iter::from_slice(slice<u8> {});
    return empty.next().is_none() && empty.next_back().is_none();
}
static_assert(byte_iteration());

template<typename T>
auto byte_item_value(const T& item) -> u8 {
    if constexpr (rstd::mtp::same_as<T, u8>)
        return item;
    else
        return item.get();
}

template<typename Make>
void check_byte_queries(Make make, Vec<u8>& owner) {
    using I      = decltype(make());
    using Item   = typename I::Item;
    using Stored = iter::stored_item_t<Item>;
    auto key     = [](const auto& value) {
        return byte_item_value(value) % u8(2);
    };

    auto chunks = iter::chunks(make(), 4_usize);
    static_assert(rstd::mtp::same_as<typename decltype(chunks)::Item, Vec<Stored>>);
    auto first = chunks.next().unwrap();
    auto moved = rstd::move(chunks);
    EXPECT_EQ(moved.next()->len(), 4_usize);
    EXPECT_EQ(moved.next()->len(), 1_usize);
    EXPECT_TRUE(moved.next().is_none());
    EXPECT_TRUE(moved.next().is_none());
    EXPECT_EQ(byte_item_value(first[0_usize]), u8(1));
    if constexpr (! rstd::mtp::same_as<Stored, u8>) {
        EXPECT_EQ(first[1_usize].as_raw_ptr(), owner.as_slice().as_raw_ptr() + 1);
    }

    auto tail = iter::take_last(make(), 5_usize).template collect<Vec<Stored>>();
    auto head = iter::skip_last(make(), 3_usize).template collect<Vec<Stored>>();
    ASSERT_EQ(tail.len(), 5_usize);
    ASSERT_EQ(head.len(), 6_usize);
    for (usize i; i < tail.len(); ++i)
        EXPECT_EQ(byte_item_value(tail[i]), u8(i.to_primitive() + 5));
    for (usize i; i < head.len(); ++i)
        EXPECT_EQ(byte_item_value(head[i]), u8(i.to_primitive() + 1));

    auto groups = iter::group_by(make(), key);
    ASSERT_EQ(groups.len(), 2_usize);
    ASSERT_EQ(groups[0_usize].items.len(), 5_usize);
    for (usize i; i < groups[0_usize].items.len(); ++i) {
        EXPECT_EQ(byte_item_value(groups[0_usize].items[i]), u8(i.to_primitive() * 2 + 1));
    }

    int  selected = 0;
    auto joined   = iter::join_by(make(), make(), key, key, [&](const auto& a, const auto& b) {
        ++selected;
        if constexpr (! rstd::mtp::same_as<Stored, u8>) {
            EXPECT_EQ(a.as_raw_ptr(),
                      owner.as_slice().as_raw_ptr() + byte_item_value(a).to_primitive() - 1);
            EXPECT_EQ(b.as_raw_ptr(),
                      owner.as_slice().as_raw_ptr() + byte_item_value(b).to_primitive() - 1);
        }
        return rstd::tuple<u8, u8>(byte_item_value(a), byte_item_value(b));
    });
    auto pair     = joined.next().unwrap();
    EXPECT_EQ(pair.template get<0>(), u8(1));
    EXPECT_EQ(pair.template get<1>(), u8(1));
    auto moved_join = rstd::move(joined);
    auto rest       = rstd::move(moved_join).template collect<Vec<rstd::tuple<u8, u8>>>();
    ASSERT_EQ(rest.len(), 40_usize);
    EXPECT_EQ(rest[0_usize].template get<1>(), u8(3));
    EXPECT_EQ(selected, 41);

    auto counts =
        iter::group_join_by(make(), make(), key, key, [](const auto&, slice<Stored> matches) {
            return matches.len();
        }).template collect<Vec<usize>>();
    ASSERT_EQ(counts.len(), 9_usize);
    for (usize i; i < counts.len(); ++i)
        EXPECT_EQ(counts[i], i % 2_usize == 0_usize ? 5_usize : 4_usize);
    {
        auto early = iter::join_by(make(), make(), key, key, [](const auto& a, const auto&) {
            return byte_item_value(a);
        });
        EXPECT_EQ(early.next().unwrap(), u8(1));
    }
    for (usize i; i < owner.len(); ++i) EXPECT_EQ(u8(owner[i]), u8(i.to_primitive() + 1));
    if constexpr (rstd::mtp::same_as<Stored, mut_ref<u8>>) {
        first[1_usize] = u8(42);
        EXPECT_EQ(u8(owner[1_usize]), u8(42));
        EXPECT_EQ(byte_item_value(head[1_usize]), u8(42));
        EXPECT_EQ(byte_item_value(groups[1_usize].items[0_usize]), u8(42));
        first[1_usize] = u8(2);
    }
}

TEST(IterStorage, ReadonlyByteQueries) {
    auto owner = iter::range(u8(1), u8(10)).collect<Vec<u8>>();
    check_byte_queries(
        [&] {
            return owner.iter();
        },
        owner);
}

TEST(IterStorage, MutableByteQueries) {
    auto owner = iter::range(u8(1), u8(10)).collect<Vec<u8>>();
    check_byte_queries(
        [&] {
            return owner.iter_mut();
        },
        owner);
}

TEST(IterStorage, CopiedByteQueries) {
    auto owner = iter::range(u8(1), u8(10)).collect<Vec<u8>>();
    check_byte_queries(
        [&] {
            return owner.iter().copied();
        },
        owner);
}

TEST(IterStorage, OwnedByteQueries) {
    auto owner = iter::range(u8(1), u8(10)).collect<Vec<u8>>();
    check_byte_queries(
        [] {
            return iter::range(u8(1), u8(10)).collect<Vec<u8>>().into_iter();
        },
        owner);
}

TEST(IterStorage, StringBytesAndEmptyBlocks) {
    auto blocks = iter::chunks("abcde"_str.bytes(), 2_usize);
    static_assert(rstd::mtp::same_as<typename decltype(blocks)::Item, Vec<u8>>);
    auto first = blocks.next().unwrap();
    EXPECT_EQ(u8(first[0_usize]), u8('a'));
    EXPECT_EQ(blocks.next()->len(), 2_usize);
    EXPECT_EQ(blocks.next()->len(), 1_usize);
    EXPECT_TRUE(blocks.next().is_none());
    EXPECT_TRUE(iter::chunks(iter::empty<mut_ref<u8>>(), 2_usize).next().is_none());
    EXPECT_TRUE(byte_iteration());
}
