module;
#include <rstd/enum.hpp>

export module rstd.alloc:collections.hash_table;
export import :alloc;
export import :collections.error;
export import rstd.core;

using namespace rstd::prelude;
using rstd::alloc::Allocator;
using rstd::alloc::Layout;
using alloc::collections::TryReserveError;

[[noreturn]]
inline void hash_allocation_failure(TryReserveError error) {
    if (error == TryReserveError::CapacityOverflow) rstd::panic { "hash table capacity overflow" };
    rstd::panic { "memory allocation failed" };
    __builtin_unreachable();
}

template<typename T, typename A>
class RawTable {
public:
    static constexpr rstd::uint8_t EMPTY   = 128;
    static constexpr rstd::uint8_t DELETED = 254;
    byte*                          data_ {};
    usize                          buckets_ {};
    usize                          items_ {};
    usize                          deleted_ {};
    RSTD_ATTR_NO_UNIQUE_ADDRESS A  allocator_;

    static auto layout(usize count) -> Option<Layout> {
        auto values = Layout::array<T>(count);
        if (values.is_none()) return None();
        auto size = values->size.checked_add(count);
        if (size.is_none()) return None();
        return Layout::from_size_align(*size, values->align);
    }
    static auto max_items(usize count) noexcept -> usize { return count - count / usize(8); }
    static auto count_for(usize capacity) -> Result<usize, TryReserveError> {
        if (capacity == usize()) return Ok(usize());
        usize count(8);
        while (max_items(count) < capacity) {
            auto next = count.checked_mul(usize(2));
            if (next.is_none()) return Err(TryReserveError::CapacityOverflow);
            count = *next;
        }
        if (layout(count).is_none()) return Err(TryReserveError::CapacityOverflow);
        return Ok(count);
    }
    auto control() const noexcept -> rstd::uint8_t* {
        return reinterpret_cast<rstd::uint8_t*>(data_ + buckets_.to_primitive() * sizeof(T));
    }
    static auto tag(u64 hash) noexcept -> rstd::uint8_t {
        return static_cast<rstd::uint8_t>(hash.to_primitive() >> 57);
    }
    auto vacant(u64 hash) const noexcept -> usize {
        auto         mask   = buckets_.to_primitive() - 1;
        auto         index  = static_cast<rstd::size_t>(hash.to_primitive()) & mask;
        rstd::size_t stride = 0;
        while (full(usize(index))) index = (index + ++stride) & mask;
        return usize(index);
    }
    void release() noexcept {
        if (! data_) return;
        clear();
        as<Allocator>(allocator_).deallocate(data_, layout(buckets_).unwrap());
        data_    = nullptr;
        buckets_ = usize();
    }
    auto allocate(usize count) -> Result<empty, TryReserveError> {
        auto checked = layout(count);
        if (checked.is_none()) return Err(TryReserveError::CapacityOverflow);
        auto allocated = as<Allocator>(allocator_).allocate(*checked);
        if (allocated.is_err()) return Err(TryReserveError::AllocError);
        data_    = allocated.unwrap_unchecked().template as_mut_ptr<byte>().as_raw_ptr();
        buckets_ = count;
        for (usize i; i < count; ++i) control()[i.to_primitive()] = EMPTY;
        return Ok(empty {});
    }
    template<typename H>
    auto rehash(usize count, H&& hash) -> Result<empty, TryReserveError> {
        if (count == usize()) {
            release();
            ++generation;
            return Ok(empty {});
        }
        // Allocate every fallible resource before moving any original element.
        RawTable<T, const A&> next(allocator_);
        auto                  allocated = next.allocate(count);
        if (allocated.is_err()) return allocated;
        struct Hashes {
            const A& allocator;
            u64*     data {};
            Layout   layout {};
            ~Hashes() {
                if (data) as<Allocator>(allocator).deallocate(data, layout);
            }
        } hashes { allocator_ };
        if constexpr (! noexcept(hash(rstd::declval<const T&>()))) {
            if (items_ != usize()) {
                auto checked = Layout::array<u64>(items_);
                if (checked.is_none()) return Err(TryReserveError::CapacityOverflow);
                auto allocated_hashes = as<Allocator>(allocator_).allocate(*checked);
                if (allocated_hashes.is_err()) return Err(TryReserveError::AllocError);
                hashes.layout = *checked;
                hashes.data =
                    allocated_hashes.unwrap_unchecked().template as_mut_ptr<u64>().as_raw_ptr();
                usize h;
                for (usize i; i < buckets_; ++i)
                    if (full(i))
                        rstd::construct_at(hashes.data + (h++).to_primitive(), hash(value(i)));
            }
        }
        usize h;
        // A throwing move may leave moved-from elements, but both storages remain owned.
        ++generation;
        for (usize i; i < buckets_; ++i)
            if (full(i)) {
                auto entry_hash = hashes.data ? hashes.data[(h++).to_primitive()] : hash(value(i));
                next.insert_at(next.vacant(entry_hash), entry_hash, rstd::move(value(i)));
            }
        release();
        data_    = rstd::exchange(next.data_, nullptr);
        buckets_ = rstd::exchange(next.buckets_, usize());
        items_   = rstd::exchange(next.items_, usize());
        deleted_ = usize();
        return Ok(empty {});
    }
    template<typename, typename>
    friend class RawTable;

public:
    rstd::uint64_t generation {};
    explicit RawTable(A allocator): allocator_(rstd::forward<A>(allocator)) {}
    RawTable(const RawTable&)            = delete;
    RawTable& operator=(const RawTable&) = delete;
    RawTable(RawTable&& other) noexcept
        : data_(rstd::exchange(other.data_, nullptr)),
          buckets_(rstd::exchange(other.buckets_, usize())),
          items_(rstd::exchange(other.items_, usize())),
          deleted_(rstd::exchange(other.deleted_, usize())),
          allocator_(rstd::move(other.allocator_)) {
        ++other.generation;
    }
    auto operator=(RawTable&& other) noexcept -> RawTable& {
        if (this != &other) {
            release();
            allocator_ = rstd::move(other.allocator_);
            data_      = rstd::exchange(other.data_, nullptr);
            buckets_   = rstd::exchange(other.buckets_, usize());
            items_     = rstd::exchange(other.items_, usize());
            deleted_   = rstd::exchange(other.deleted_, usize());
            ++generation;
            ++other.generation;
        }
        return *this;
    }
    ~RawTable() { release(); }
    auto allocator() const noexcept -> const A& { return allocator_; }
    auto len() const noexcept -> usize { return items_; }
    auto capacity() const noexcept -> usize { return max_items(buckets_); }
    auto bucket_count() const noexcept -> usize { return buckets_; }
    auto allocation_size() const noexcept -> usize {
        return data_ ? layout(buckets_).unwrap().size : usize();
    }
    auto full(usize index) const noexcept -> bool {
        return control()[index.to_primitive()] < EMPTY;
    }
    auto value(usize index) noexcept -> T& {
        return *reinterpret_cast<T*>(data_ + index.to_primitive() * sizeof(T));
    }
    auto value(usize index) const noexcept -> const T& {
        return *reinterpret_cast<const T*>(data_ + index.to_primitive() * sizeof(T));
    }
    // Full semantic equality is required even when fingerprints collide.
    template<typename E>
    auto find(u64 hash, E&& equal) const -> Option<usize> {
        if (! data_) return None();
        auto         mask   = buckets_.to_primitive() - 1;
        auto         index  = static_cast<rstd::size_t>(hash.to_primitive()) & mask;
        rstd::size_t stride = 0;
        for (usize visited; visited < buckets_; ++visited) {
            auto state = control()[index];
            if (state == EMPTY) return None();
            if (state == tag(hash) && equal(value(usize(index)))) return Some(usize(index));
            index = (index + ++stride) & mask;
        }
        return None();
    }
    template<typename H>
    auto try_reserve(usize additional, H&& hash) -> Result<empty, TryReserveError> {
        if (additional == usize() || additional <= capacity() - items_ - deleted_)
            return Ok(empty {});
        auto required = items_.checked_add(additional);
        if (required.is_none()) return Err(TryReserveError::CapacityOverflow);
        auto count = count_for(*required);
        if (count.is_err()) return Err(count.unwrap_err_unchecked());
        return rehash(rstd::cmp::max(*count, buckets_), hash);
    }
    template<typename H>
    auto try_shrink_to(usize minimum, H&& hash) -> Result<empty, TryReserveError> {
        auto count = count_for(rstd::cmp::max(minimum, items_));
        if (count.is_err()) return Err(count.unwrap_err_unchecked());
        if (*count < buckets_ || deleted_ != usize())
            return rehash(rstd::cmp::min(*count, buckets_), hash);
        return Ok(empty {});
    }
    auto insertion_slot(u64 hash) const noexcept -> usize { return vacant(hash); }
    void insert_at(usize index, u64 hash, T value) {
        auto state = control()[index.to_primitive()];
        debug_assert(state >= EMPTY);
        rstd::construct_at(reinterpret_cast<T*>(data_ + index.to_primitive() * sizeof(T)),
                           rstd::move(value));
        if (state == DELETED) --deleted_;
        control()[index.to_primitive()] = tag(hash);
        ++items_;
        ++generation;
    }
    auto remove(usize index) -> T {
        debug_assert(full(index));
        T result = rstd::move(value(index));
        erase(index);
        return result;
    }
    void erase(usize index) noexcept {
        debug_assert(full(index));
        rstd::destroy_at(rstd::addressof(value(index)));
        control()[index.to_primitive()] = DELETED;
        --items_;
        ++deleted_;
        ++generation;
    }
    void clear() noexcept {
        for (usize i; i < buckets_; ++i) {
            if (full(i)) rstd::destroy_at(rstd::addressof(value(i)));
            control()[i.to_primitive()] = EMPTY;
        }
        items_   = usize();
        deleted_ = usize();
        ++generation;
    }
};

export namespace alloc::collections
{
// Hash/equality callbacks borrow external state only for the call. Rehash must reproduce
// insertion hashes; mutable access must preserve identity. Structural changes invalidate
// borrowed references, iterators and entry handles. Callbacks must not mutate this table.
template<typename T, typename A = ::alloc::Global>
class HashTable {
    RawTable<T, A> table_;

public:
    class OccupiedEntry {
        RawTable<T, A>* table_;
        usize           index_;
        rstd::uint64_t  generation_;
        friend class HashTable;
        OccupiedEntry(RawTable<T, A>* table, usize index)
            : table_(table), index_(index), generation_(table->generation) {}
        void check() const { debug_assert(table_ && generation_ == table_->generation); }

    public:
        OccupiedEntry(const OccupiedEntry&)                    = delete;
        auto operator=(const OccupiedEntry&) -> OccupiedEntry& = delete;
        OccupiedEntry(OccupiedEntry&& other) noexcept
            : table_(rstd::exchange(other.table_, nullptr)),
              index_(other.index_),
              generation_(other.generation_) {}
        auto operator=(OccupiedEntry&& other) noexcept -> OccupiedEntry& {
            table_      = rstd::exchange(other.table_, nullptr);
            index_      = other.index_;
            generation_ = other.generation_;
            return *this;
        }
        auto get() const [[clang::lifetimebound]] -> const T& {
            check();
            return table_->value(index_);
        }
        auto get_mut() [[clang::lifetimebound]] -> T& {
            check();
            return table_->value(index_);
        }
        auto remove() && -> T {
            check();
            auto* table = rstd::exchange(table_, nullptr);
            return table->remove(index_);
        }
        void erase() && noexcept {
            check();
            auto* table = rstd::exchange(table_, nullptr);
            table->erase(index_);
        }
    };
    class VacantEntry {
        RawTable<T, A>* table_;
        usize           index_;
        u64             hash_;
        rstd::uint64_t  generation_;
        friend class HashTable;
        VacantEntry(RawTable<T, A>* table, usize index, u64 hash)
            : table_(table), index_(index), hash_(hash), generation_(table->generation) {}

    public:
        VacantEntry(const VacantEntry&)                    = delete;
        auto operator=(const VacantEntry&) -> VacantEntry& = delete;
        VacantEntry(VacantEntry&& other) noexcept
            : table_(rstd::exchange(other.table_, nullptr)),
              index_(other.index_),
              hash_(other.hash_),
              generation_(other.generation_) {}
        auto operator=(VacantEntry&& other) noexcept -> VacantEntry& {
            table_      = rstd::exchange(other.table_, nullptr);
            index_      = other.index_;
            hash_       = other.hash_;
            generation_ = other.generation_;
            return *this;
        }
        auto insert(T value) && -> OccupiedEntry {
            debug_assert(table_ && generation_ == table_->generation);
            auto* table = rstd::exchange(table_, nullptr);
            table->insert_at(index_, hash_, rstd::move(value));
            return OccupiedEntry(table, index_);
        }
    };
    class Entry {
        RSTD_ENUM(Entry, (Occupied, (OccupiedEntry value;)), (Vacant, (VacantEntry value;)))
    };
    class Iter : public rstd::DefaultInClass<Iter, rstd::iter::Iterator> {
        const RawTable<T, A>* table_;
        usize                 index_ {};
        usize                 remaining_;
        rstd::uint64_t        generation_;

    public:
        using Item                               = rstd::ref<T>;
        static constexpr bool PROVEN_EXACT_SIZE  = true;
        static constexpr bool PROVEN_FUSED       = true;
        static constexpr bool PROVEN_TRUSTED_LEN = true;
        explicit Iter(const RawTable<T, A>* table [[clang::lifetimebound]])
            : table_(table), remaining_(table->len()), generation_(table->generation) {}
        auto next() -> Option<Item> {
            debug_assert(generation_ == table_->generation);
            while (remaining_ != usize() && index_ < table_->bucket_count()) {
                auto index = index_++;
                if (! table_->full(index)) continue;
                --remaining_;
                return Some(Item::from_raw_parts(rstd::addressof(table_->value(index))));
            }
            return None();
        }
        auto len() const noexcept -> usize { return remaining_; }
        auto size_hint() const -> rstd::iter::SizeHint {
            return { remaining_, Some(usize(remaining_)) };
        }
    };
    class IterMut : public rstd::DefaultInClass<IterMut, rstd::iter::Iterator> {
        RawTable<T, A>* table_;
        usize           index_ {};
        usize           remaining_;
        rstd::uint64_t  generation_;

    public:
        using Item                               = rstd::mut_ref<T>;
        static constexpr bool PROVEN_EXACT_SIZE  = true;
        static constexpr bool PROVEN_FUSED       = true;
        static constexpr bool PROVEN_TRUSTED_LEN = true;
        explicit IterMut(RawTable<T, A>* table [[clang::lifetimebound]])
            : table_(table), remaining_(table->len()), generation_(table->generation) {}
        auto next() -> Option<Item> {
            debug_assert(generation_ == table_->generation);
            while (remaining_ != usize() && index_ < table_->bucket_count()) {
                auto index = index_++;
                if (! table_->full(index)) continue;
                --remaining_;
                return Some(Item::from_raw_parts(rstd::addressof(table_->value(index))));
            }
            return None();
        }
        auto len() const noexcept -> usize { return remaining_; }
        auto size_hint() const -> rstd::iter::SizeHint {
            return { remaining_, Some(usize(remaining_)) };
        }
    };
    class IntoIter : public rstd::DefaultInClass<IntoIter, rstd::iter::Iterator> {
        RawTable<T, A> table_;
        usize          index_ {};

    public:
        using Item                               = T;
        static constexpr bool PROVEN_EXACT_SIZE  = true;
        static constexpr bool PROVEN_FUSED       = true;
        static constexpr bool PROVEN_TRUSTED_LEN = true;
        explicit IntoIter(RawTable<T, A> table): table_(rstd::move(table)) {}
        auto next() -> Option<T> {
            while (index_ < table_.bucket_count()) {
                auto index = index_++;
                if (table_.full(index)) return Some(table_.remove(index));
            }
            return None();
        }
        auto len() const noexcept -> usize { return table_.len(); }
        auto size_hint() const -> rstd::iter::SizeHint { return { len(), Some(len()) }; }
    };
    HashTable()
        requires rstd::mtp::init<A>
        : table_(A {}) {}
    explicit HashTable(A allocator): table_(rstd::move(allocator)) {}
    HashTable(const HashTable&)                               = delete;
    auto operator=(const HashTable&) -> HashTable&            = delete;
    HashTable(HashTable&&) noexcept                           = default;
    auto        operator=(HashTable&&) noexcept -> HashTable& = default;
    static auto new_in(A allocator) -> HashTable { return HashTable(rstd::move(allocator)); }
    static auto with_capacity_in(usize capacity, A allocator) -> HashTable {
        HashTable result(rstd::move(allocator));
        result.reserve(capacity, [](const T&) noexcept -> u64 {
            return u64();
        });
        return result;
    }
    static auto with_capacity(usize capacity) -> HashTable
        requires rstd::mtp::init<A>
    {
        return with_capacity_in(capacity, A {});
    }
    auto allocator() const noexcept [[clang::lifetimebound]] -> const A& {
        return table_.allocator();
    }
    auto len() const noexcept -> usize { return table_.len(); }
    auto is_empty() const noexcept -> bool { return len() == usize(); }
    auto capacity() const noexcept -> usize { return table_.capacity(); }
    // Requested table storage only; excludes indirect allocations owned by T.
    auto allocation_size() const noexcept -> usize { return table_.allocation_size(); }
    // Full semantic equality is required even when fingerprints collide.
    template<typename E>
    auto find(u64 hash, E&& equal) const [[clang::lifetimebound]] -> Option<ref<T>> {
        auto index = table_.find(hash, equal);
        if (index.is_none()) return None();
        return Some(ref<T>::from_raw_parts(rstd::addressof(table_.value(*index))));
    }
    template<typename E>
    auto find_mut(u64 hash, E&& equal) [[clang::lifetimebound]] -> Option<mut_ref<T>> {
        auto index = table_.find(hash, equal);
        if (index.is_none()) return None();
        return Some(mut_ref<T>::from_raw_parts(rstd::addressof(table_.value(*index))));
    }
    template<typename E>
    auto find_entry(u64 hash, E&& equal) [[clang::lifetimebound]] -> Option<OccupiedEntry> {
        auto index = table_.find(hash, equal);
        if (index.is_none()) return None();
        return Some(OccupiedEntry(&table_, *index));
    }
    template<typename E, typename H>
    auto try_entry(u64 hash, E&& equal, H&& rehash) [[clang::lifetimebound]]
    -> Result<Entry, TryReserveError> {
        auto found = table_.find(hash, equal);
        if (found.is_some()) return Ok(Entry::Occupied(OccupiedEntry(&table_, *found)));
        auto reserved = try_reserve(usize(1), rehash);
        if (reserved.is_err()) return Err(reserved.unwrap_err_unchecked());
        return Ok(Entry::Vacant(VacantEntry(&table_, table_.insertion_slot(hash), hash)));
    }
    template<typename E, typename H>
    auto entry(u64 hash, E&& equal, H&& rehash) [[clang::lifetimebound]] -> Entry {
        auto result = try_entry(hash, equal, rehash);
        if (result.is_err()) hash_allocation_failure(result.unwrap_err_unchecked());
        return rstd::move(result).unwrap_unchecked();
    }
    template<typename H>
    auto try_reserve(usize additional, H&& hash) -> Result<empty, TryReserveError> {
        return table_.try_reserve(additional, hash);
    }
    template<typename H>
    void reserve(usize additional, H&& hash) {
        auto result = try_reserve(additional, hash);
        if (result.is_err()) hash_allocation_failure(result.unwrap_err_unchecked());
    }
    template<typename H>
    auto try_shrink_to(usize minimum, H&& hash) -> Result<empty, TryReserveError> {
        return table_.try_shrink_to(minimum, hash);
    }
    template<typename H>
    void shrink_to(usize minimum, H&& hash) {
        auto result = try_shrink_to(minimum, hash);
        if (result.is_err()) hash_allocation_failure(result.unwrap_err_unchecked());
    }
    void clear() noexcept { table_.clear(); }
    template<typename F>
    void retain(F predicate) {
        for (usize i; i < table_.bucket_count(); ++i)
            if (table_.full(i) && ! predicate(table_.value(i))) table_.erase(i);
    }
    auto iter() const [[clang::lifetimebound]] -> Iter { return Iter(&table_); }
    auto iter_mut() [[clang::lifetimebound]] -> IterMut { return IterMut(&table_); }
    auto into_iter() && -> IntoIter { return IntoIter(rstd::move(table_)); }
};
} // namespace alloc::collections
