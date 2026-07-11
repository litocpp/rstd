module;
#include <rstd/macro.hpp>

export module rstd.alloc:collections.hash_table;
export import :alloc;
export import rstd.core;

namespace alloc::collections::hash::detail
{

using rstd::alloc::Allocator;
using rstd::alloc::Layout;
using rstd::mem::maybe_uninit::MaybeUninit;
using rstd::ptr_::non_null::NonNull;
using namespace rstd::prelude;

enum class BucketState : u8
{
    Empty,
    Full,
    Deleted
};

template<typename K, typename V>
class Bucket {
    MaybeUninit<K> key_slot;
    MaybeUninit<V> value_slot;

public:
    u64         hash;
    BucketState state;

    Bucket(): hash(0), state(BucketState::Empty) {}
    Bucket(const Bucket&)            = delete;
    Bucket& operator=(const Bucket&) = delete;

    ~Bucket() { clear(); }

    auto key() noexcept -> K& { return key_slot.assume_init_mut(); }
    auto key() const noexcept -> const K& { return key_slot.assume_init_ref(); }
    auto value() noexcept -> V& { return value_slot.assume_init_mut(); }
    auto value() const noexcept -> const V& { return value_slot.assume_init_ref(); }

    auto replace_value(V replacement) -> V {
        V old = rstd::move(value());
        value_slot.assume_init_drop();
        value_slot.write(rstd::move(replacement));
        return old;
    }

    void write(u64 entry_hash, K key, V value) {
        hash = entry_hash;
        key_slot.write(rstd::move(key));
        value_slot.write(rstd::move(value));
        state = BucketState::Full;
    }

    auto take() -> rstd::tuple<K, V> {
        K key   = rstd::move(this->key());
        V value = rstd::move(this->value());
        key_slot.assume_init_drop();
        value_slot.assume_init_drop();
        state = BucketState::Deleted;
        return { rstd::move(key), rstd::move(value) };
    }

    void clear() noexcept {
        if (state == BucketState::Full) {
            key_slot.assume_init_drop();
            value_slot.assume_init_drop();
        }
        state = BucketState::Empty;
    }
};

export template<typename K, typename V>
class RawTable {
    Bucket<K, V>* data;
    usize         buckets;
    usize         items;
    usize         deleted;

    static auto max_items(usize bucket_count) noexcept -> usize {
        return bucket_count - bucket_count / 8;
    }

    static auto bucket_count_for(usize capacity) -> usize {
        if (capacity == 0) return 0;
        usize count = 8;
        while (max_items(count) < capacity) count *= 2;
        return count;
    }

    void allocate(usize count) {
        if (count == 0) return;
        auto layout = Layout::array<Bucket<K, V>>(count).unwrap();
        auto result = as<Allocator>(::alloc::GLOBAL).allocate(layout);
        if (result.is_err()) ::alloc::handle_alloc_error(layout);
        data = reinterpret_cast<Bucket<K, V>*>(result.unwrap_unchecked().as_mut_ptr().as_raw_ptr());
        buckets = count;
        for (usize i = 0; i < buckets; ++i) rstd::construct_at(data + i);
    }

    void release() noexcept {
        if (data == nullptr) return;
        for (usize i = 0; i < buckets; ++i) rstd::destroy_at(data + i);
        auto layout = Layout::array<Bucket<K, V>>(buckets).unwrap();
        as<Allocator>(::alloc::GLOBAL)
            .deallocate(NonNull<u8>::make_unchecked(
                            mut_ptr<u8>::from_raw_parts(reinterpret_cast<u8*>(data))),
                        layout);
        data    = nullptr;
        buckets = 0;
        items   = 0;
        deleted = 0;
    }

    void insert_rehashed(u64 hash, K key, V value) {
        usize index  = static_cast<usize>(hash) & (buckets - 1);
        usize stride = 0;
        for (;;) {
            if (data[index].state != BucketState::Full) {
                if (data[index].state == BucketState::Deleted) --deleted;
                data[index].write(hash, rstd::move(key), rstd::move(value));
                ++items;
                return;
            }
            index = (index + ++stride) & (buckets - 1);
        }
    }

    void rehash(usize count) {
        RawTable replacement;
        replacement.allocate(count);
        for (usize i = 0; i < buckets; ++i) {
            if (data[i].state != BucketState::Full) continue;
            u64  hash  = data[i].hash;
            auto entry = data[i].take();
            replacement.insert_rehashed(
                hash, rstd::move(entry.template get<0>()), rstd::move(entry.template get<1>()));
        }
        *this = rstd::move(replacement);
    }

    bool valid() const noexcept {
        if (buckets == 0) return data == nullptr && items == 0 && deleted == 0;
        if (data == nullptr || (buckets & (buckets - 1)) != 0) return false;
        usize full_count    = 0;
        usize deleted_count = 0;
        for (usize i = 0; i < buckets; ++i) {
            if (data[i].state == BucketState::Full) ++full_count;
            if (data[i].state == BucketState::Deleted) ++deleted_count;
        }
        return full_count == items && deleted_count == deleted && items + deleted <= capacity();
    }

public:
    RawTable(): data(nullptr), buckets(0), items(0), deleted(0) {}
    explicit RawTable(usize capacity): RawTable() { allocate(bucket_count_for(capacity)); }
    RawTable(const RawTable&)            = delete;
    RawTable& operator=(const RawTable&) = delete;
    RawTable(RawTable&& other) noexcept
        : data(other.data), buckets(other.buckets), items(other.items), deleted(other.deleted) {
        other.data    = nullptr;
        other.buckets = 0;
        other.items   = 0;
        other.deleted = 0;
    }
    RawTable& operator=(RawTable&& other) noexcept {
        if (this != rstd::addressof(other)) {
            release();
            data          = other.data;
            buckets       = other.buckets;
            items         = other.items;
            deleted       = other.deleted;
            other.data    = nullptr;
            other.buckets = 0;
            other.items   = 0;
            other.deleted = 0;
        }
        return *this;
    }
    ~RawTable() { release(); }

    auto len() const noexcept -> usize { return items; }
    auto bucket_count() const noexcept -> usize { return buckets; }
    auto capacity() const noexcept -> usize { return max_items(buckets); }
    auto bucket(usize index) noexcept -> Bucket<K, V>& { return data[index]; }
    auto bucket(usize index) const noexcept -> const Bucket<K, V>& { return data[index]; }

    template<typename Equal>
    auto find(u64 hash, Equal equal) const -> Option<usize> {
        if (buckets == 0) return None();
        usize index  = static_cast<usize>(hash) & (buckets - 1);
        usize stride = 0;
        for (usize visited = 0; visited < buckets; ++visited) {
            const auto& entry = data[index];
            if (entry.state == BucketState::Empty) return None();
            if (entry.state == BucketState::Full && entry.hash == hash && equal(entry.key())) {
                return Some(index);
            }
            index = (index + ++stride) & (buckets - 1);
        }
        return None();
    }

    void reserve(usize additional) {
        usize required = items + additional;
        if (required <= capacity() && items + deleted + additional <= capacity()) return;
        usize count = buckets == 0 ? bucket_count_for(required) : buckets;
        while (max_items(count) < required) count *= 2;
        rehash(count);
        debug_assert(valid());
    }

    void insert(u64 hash, K key, V value) {
        reserve(1);
        insert_rehashed(hash, rstd::move(key), rstd::move(value));
        debug_assert(valid());
    }

    auto remove(usize index) -> rstd::tuple<K, V> {
        auto entry = data[index].take();
        --items;
        ++deleted;
        debug_assert(valid());
        return entry;
    }

    void clear() noexcept {
        for (usize i = 0; i < buckets; ++i) data[i].clear();
        items   = 0;
        deleted = 0;
        debug_assert(valid());
    }

    void shrink_to(usize minimum) {
        usize required = items > minimum ? items : minimum;
        usize count    = bucket_count_for(required);
        if (count < buckets || deleted != 0) rehash(count);
        debug_assert(valid());
    }
};

} // namespace alloc::collections::hash::detail
