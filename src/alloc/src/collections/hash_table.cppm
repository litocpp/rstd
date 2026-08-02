module;
#include <rstd/macro.hpp>

export module rstd.alloc:collections.hash_table;
export import :alloc;
export import rstd.core;

using rstd::alloc::Allocator;
using rstd::alloc::Layout;
using rstd::mem::maybe_uninit::MaybeUninit;
using rstd::ptr_::non_null::NonNull;
using namespace rstd::prelude;

enum class BucketState : rstd::uint8_t
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

template<typename K, typename V>
class RawTable {
    Bucket<K, V>* data;
    usize         buckets;
    usize         items;
    usize         deleted;

    static auto max_items(usize bucket_count) noexcept -> usize {
        return bucket_count - bucket_count / usize(8);
    }

    static auto bucket_count_for(usize capacity) -> usize {
        if (capacity == usize()) return usize();
        usize count = usize(8);
        while (max_items(count) < capacity) count *= usize(2);
        return count;
    }

    void allocate(usize count) {
        if (count == usize()) return;
        auto layout = Layout::array<Bucket<K, V>>(count).unwrap();
        auto result = as<Allocator>(::alloc::GLOBAL).allocate(layout);
        if (result.is_err()) ::alloc::handle_alloc_error(layout);
        data    = result.unwrap_unchecked().template as_mut_ptr<Bucket<K, V>>().as_raw_ptr();
        buckets = count;
        for (rstd::size_t i = 0; i < buckets.to_primitive(); ++i) rstd::construct_at(data + i);
    }

    void release() noexcept {
        if (data == nullptr) return;
        for (rstd::size_t i = 0; i < buckets.to_primitive(); ++i) rstd::destroy_at(data + i);
        auto layout = Layout::array<Bucket<K, V>>(buckets).unwrap();
        as<Allocator>(::alloc::GLOBAL).deallocate(data, layout);
        data    = nullptr;
        buckets = usize();
        items   = usize();
        deleted = usize();
    }

    void insert_rehashed(u64 hash, K key, V value) {
        auto         bucket_mask = buckets.to_primitive() - 1;
        auto         index       = static_cast<rstd::size_t>(hash.to_primitive()) & bucket_mask;
        rstd::size_t stride      = 0;
        for (;;) {
            if (data[index].state != BucketState::Full) {
                if (data[index].state == BucketState::Deleted) --deleted;
                data[index].write(hash, rstd::move(key), rstd::move(value));
                ++items;
                return;
            }
            ++stride;
            index = (index + stride) & bucket_mask;
        }
    }

    void rehash(usize count) {
        RawTable replacement;
        replacement.allocate(count);
        for (rstd::size_t i = 0; i < buckets.to_primitive(); ++i) {
            if (data[i].state != BucketState::Full) continue;
            u64  hash  = data[i].hash;
            auto entry = data[i].take();
            replacement.insert_rehashed(
                hash, rstd::move(entry.template get<0>()), rstd::move(entry.template get<1>()));
        }
        *this = rstd::move(replacement);
    }

    bool valid() const noexcept {
        if (buckets == usize()) {
            return data == nullptr && items == usize() && deleted == usize();
        }
        return data != nullptr && (buckets & (buckets - usize(1))) == usize() &&
               items + deleted <= capacity();
    }

    // A full audit is linear in bucket count and must not run after every mutation.
    bool audit_valid() const noexcept {
        if (! valid()) return false;
        usize full_count    = usize();
        usize deleted_count = usize();
        for (rstd::size_t i = 0; i < buckets.to_primitive(); ++i) {
            if (data[i].state == BucketState::Full) ++full_count;
            if (data[i].state == BucketState::Deleted) ++deleted_count;
        }
        return full_count == items && deleted_count == deleted;
    }

public:
    RawTable(): data(nullptr), buckets(usize()), items(usize()), deleted(usize()) {}
    explicit RawTable(usize capacity): RawTable() { allocate(bucket_count_for(capacity)); }
    RawTable(const RawTable&)            = delete;
    RawTable& operator=(const RawTable&) = delete;
    RawTable(RawTable&& other) noexcept
        : data(other.data), buckets(other.buckets), items(other.items), deleted(other.deleted) {
        other.data    = nullptr;
        other.buckets = usize();
        other.items   = usize();
        other.deleted = usize();
    }
    RawTable& operator=(RawTable&& other) noexcept {
        if (this != rstd::addressof(other)) {
            release();
            data          = other.data;
            buckets       = other.buckets;
            items         = other.items;
            deleted       = other.deleted;
            other.data    = nullptr;
            other.buckets = usize();
            other.items   = usize();
            other.deleted = usize();
        }
        return *this;
    }
    ~RawTable() { release(); }

    auto len() const noexcept -> usize { return items; }
    auto bucket_count() const noexcept -> usize { return buckets; }
    auto capacity() const noexcept -> usize { return max_items(buckets); }
    auto bucket(rstd::size_t index) noexcept -> Bucket<K, V>& { return data[index]; }
    auto bucket(rstd::size_t index) const noexcept -> const Bucket<K, V>& { return data[index]; }
    auto bucket(usize index) noexcept -> Bucket<K, V>& { return data[index.to_primitive()]; }
    auto bucket(usize index) const noexcept -> const Bucket<K, V>& {
        return data[index.to_primitive()];
    }

    template<typename Equal>
    auto find(u64 hash, Equal equal) const -> Option<usize> {
        if (buckets == usize()) return None();
        auto         bucket_mask = buckets.to_primitive() - 1;
        auto         index       = static_cast<rstd::size_t>(hash.to_primitive()) & bucket_mask;
        rstd::size_t stride      = 0;
        for (rstd::size_t visited = 0; visited < buckets.to_primitive(); ++visited) {
            const auto& entry = data[index];
            if (entry.state == BucketState::Empty) return None();
            if (entry.state == BucketState::Full && entry.hash == hash && equal(entry.key())) {
                return Some(usize(index));
            }
            ++stride;
            index = (index + stride) & bucket_mask;
        }
        return None();
    }

    void reserve(usize additional) {
        usize required = items + additional;
        if (required <= capacity() && items + deleted + additional <= capacity()) return;
        usize count = buckets == usize() ? bucket_count_for(required) : buckets;
        while (max_items(count) < required) count *= usize(2);
        rehash(count);
        debug_assert(audit_valid());
    }

    void insert(u64 hash, K key, V value) {
        reserve(usize(1));
        insert_rehashed(hash, rstd::move(key), rstd::move(value));
        debug_assert(valid());
    }

    auto remove(usize index) -> rstd::tuple<K, V> {
        auto entry = data[index.to_primitive()].take();
        --items;
        ++deleted;
        debug_assert(valid());
        return entry;
    }

    void clear() noexcept {
        for (rstd::size_t i = 0; i < buckets.to_primitive(); ++i) data[i].clear();
        items   = usize();
        deleted = usize();
        debug_assert(audit_valid());
    }

    void shrink_to(usize minimum) {
        usize required = items > minimum ? items : minimum;
        usize count    = bucket_count_for(required);
        if (count < buckets || deleted != usize()) rehash(count);
        debug_assert(audit_valid());
    }
};
