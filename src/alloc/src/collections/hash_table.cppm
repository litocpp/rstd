module;
#include <rstd/macro.hpp>

export module rstd.alloc:collections.hash_table;
export import :alloc;
export import :collections.error;
export import rstd.core;

using rstd::alloc::Allocator;
using rstd::alloc::Layout;
using rstd::mem::maybe_uninit::MaybeUninit;
using rstd::ptr_::non_null::NonNull;
using namespace rstd::prelude;
using alloc::collections::TryReserveError;

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
        struct KeyGuard {
            Bucket* bucket;
            ~KeyGuard() {
                if (bucket) bucket->key_slot.assume_init_drop();
            }
        } guard { this };
        value_slot.write(rstd::move(value));
        guard.bucket = nullptr;
        state        = BucketState::Full;
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

template<typename K, typename V, typename A = ::alloc::Global>
class RawTable {
    Bucket<K, V>*                 data;
    usize                         buckets;
    usize                         items;
    usize                         deleted;
    RSTD_ATTR_NO_UNIQUE_ADDRESS A allocator;

    static auto max_items(usize bucket_count) noexcept -> usize {
        return bucket_count - bucket_count / usize(8);
    }

    static auto bucket_count_for(usize capacity) -> Result<usize, TryReserveError> {
        if (capacity == usize()) return Ok(usize());
        usize count = usize(8);
        while (max_items(count) < capacity) {
            auto next = count.checked_mul(usize(2));
            if (next.is_none()) return Err(TryReserveError::CapacityOverflow);
            count = next.unwrap_unchecked();
        }
        if (Layout::array<Bucket<K, V>>(count).is_none())
            return Err(TryReserveError::CapacityOverflow);
        return Ok(count);
    }

    struct Storage {
        const A&      allocator;
        Bucket<K, V>* data;
        usize         buckets;
        ~Storage() {
            if (! data) return;
            for (rstd::size_t i = 0; i < buckets.to_primitive(); ++i) rstd::destroy_at(data + i);
            as<Allocator>(allocator).deallocate(data,
                                                Layout::array<Bucket<K, V>>(buckets).unwrap());
        }
    };

    auto try_allocate(usize count) -> Result<Bucket<K, V>*, TryReserveError> {
        if (count == usize()) return Ok(static_cast<Bucket<K, V>*>(nullptr));
        auto layout = Layout::array<Bucket<K, V>>(count);
        if (layout.is_none()) return Err(TryReserveError::CapacityOverflow);
        auto result = as<Allocator>(allocator).allocate(*layout);
        if (result.is_err()) return Err(TryReserveError::AllocError);
        auto* pointer = result.unwrap_unchecked().template as_mut_ptr<Bucket<K, V>>().as_raw_ptr();
        for (rstd::size_t i = 0; i < count.to_primitive(); ++i) rstd::construct_at(pointer + i);
        return Ok(pointer);
    }

    [[noreturn]]
    static void allocation_failure(TryReserveError error) {
        if (error == TryReserveError::CapacityOverflow)
            rstd::panic { "hash table capacity overflow" };
        rstd::panic { "memory allocation failed" };
        __builtin_unreachable();
    }

    void release() noexcept {
        if (data == nullptr) return;
        for (rstd::size_t i = 0; i < buckets.to_primitive(); ++i) rstd::destroy_at(data + i);
        auto layout = Layout::array<Bucket<K, V>>(buckets).unwrap();
        as<Allocator>(allocator).deallocate(data, layout);
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

    auto try_rehash(usize count) -> Result<empty, TryReserveError> {
        if (count == usize()) {
            release();
            return Ok(empty {});
        }
        auto allocated = try_allocate(count);
        if (allocated.is_err()) return Err(allocated.unwrap_err_unchecked());
        Storage next { allocator, allocated.unwrap_unchecked(), count };
        Storage old { allocator, data, buckets };
        data    = rstd::exchange(next.data, nullptr);
        buckets = count;
        items   = usize();
        deleted = usize();
        for (rstd::size_t i = 0; i < old.buckets.to_primitive(); ++i) {
            if (old.data[i].state != BucketState::Full) continue;
            u64  hash  = old.data[i].hash;
            auto entry = old.data[i].take();
            insert_rehashed(
                hash, rstd::move(entry.template get<0>()), rstd::move(entry.template get<1>()));
        }
        return Ok(empty {});
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
    RawTable()
        requires rstd::mtp::init<A>
        : RawTable(A {}) {}
    explicit RawTable(A allocator)
        : data(nullptr),
          buckets(usize()),
          items(usize()),
          deleted(usize()),
          allocator(rstd::move(allocator)) {}
    RawTable(usize capacity, A allocator): RawTable(rstd::move(allocator)) {
        auto count = bucket_count_for(capacity);
        if (count.is_err()) allocation_failure(count.unwrap_err_unchecked());
        auto result = try_rehash(count.unwrap_unchecked());
        if (result.is_err()) allocation_failure(result.unwrap_err_unchecked());
    }
    explicit RawTable(usize capacity)
        requires rstd::mtp::init<A>
        : RawTable(capacity, A {}) {}
    RawTable(const RawTable&)            = delete;
    RawTable& operator=(const RawTable&) = delete;
    RawTable(RawTable&& other) noexcept
        : data(other.data),
          buckets(other.buckets),
          items(other.items),
          deleted(other.deleted),
          allocator(rstd::move(other.allocator)) {
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
            allocator     = rstd::move(other.allocator);
            other.data    = nullptr;
            other.buckets = usize();
            other.items   = usize();
            other.deleted = usize();
        }
        return *this;
    }
    ~RawTable() { release(); }

    auto len() const noexcept -> usize { return items; }
    auto allocator_ref() const noexcept [[clang::lifetimebound]] -> const A& { return allocator; }
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

    auto try_reserve(usize additional) -> Result<empty, TryReserveError> {
        if (additional == usize()) return Ok(empty {});
        // Subtract known occupied slots instead of overflowing a three-term sum.
        if (additional <= capacity() - items - deleted) return Ok(empty {});
        auto required = items.checked_add(additional);
        if (required.is_none()) return Err(TryReserveError::CapacityOverflow);
        auto count = bucket_count_for(required.unwrap_unchecked());
        if (count.is_err()) return Err(count.unwrap_err_unchecked());
        auto result = try_rehash(rstd::cmp::max(count.unwrap_unchecked(), buckets));
        if (result.is_ok()) debug_assert(audit_valid());
        return result;
    }

    void reserve(usize additional) {
        auto result = try_reserve(additional);
        if (result.is_err()) allocation_failure(result.unwrap_err_unchecked());
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
        auto  checked  = bucket_count_for(required);
        if (checked.is_err()) allocation_failure(checked.unwrap_err_unchecked());
        usize count = checked.unwrap_unchecked();
        if (count < buckets || deleted != usize()) {
            auto result = try_rehash(count);
            if (result.is_err()) allocation_failure(result.unwrap_err_unchecked());
        }
        debug_assert(audit_valid());
    }
};
