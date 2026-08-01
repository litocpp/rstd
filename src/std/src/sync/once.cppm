module;
#include <rstd/macro.hpp>
export module rstd:sync.once;
import :sys.sync.once;
export import rstd.core;

namespace rstd::sync
{
namespace once_detail
{
using SysOnce        = rstd::sys::sync::once::futex::Once;
using ExclusiveState = rstd::sys::sync::once::futex::ExclusiveState;
} // namespace once_detail

export class OnceState {
    constexpr OnceState() noexcept = default;

    friend class Once;

public:
    // TODO: Report poisoning after rstd panic supports stack unwinding.
    constexpr auto is_poisoned() const noexcept -> bool { return false; }
};

export template<typename T>
class OnceLock;

export template<typename T, typename F = T (*)()>
class LazyLock;

export class Once {
    once_detail::SysOnce m_inner;

    template<typename>
    friend class OnceLock;

    template<typename, typename>
    friend class LazyLock;

    auto exclusive_state() & noexcept -> once_detail::ExclusiveState { return m_inner.state(); }

    void set_exclusive_state(once_detail::ExclusiveState state) & noexcept {
        m_inner.set_state(state);
    }

public:
    constexpr Once() noexcept = default;

    static constexpr auto make() noexcept -> Once { return {}; }

    Once(Once const&)                    = delete;
    auto operator=(Once const&) -> Once& = delete;
    Once(Once&&)                         = delete;
    auto operator=(Once&&) -> Once&      = delete;

    template<typename F>
        requires requires(F&& initializer) {
            rstd::invoke_once<void()>(rstd::forward<F>(initializer));
        }
    void call_once(F&& initializer) const {
        if (m_inner.is_completed()) return;

        using Initializer = mtp::rm_ref<F>;
        auto callback     = +[](void* context) {
            auto& value = *static_cast<Initializer*>(context);
            rstd::invoke_once<void()>(rstd::forward<F>(value));
        };
        auto* context = const_cast<void*>(static_cast<void const*>(rstd::addressof(initializer)));
        m_inner.call(context, callback);
    }

    template<typename F>
        requires requires(F&& initializer, OnceState const& state) {
            rstd::invoke_once<void(OnceState const&)>(rstd::forward<F>(initializer), state);
        }
    void call_once_force(F&& initializer) const {
        if (m_inner.is_completed()) return;

        using Initializer = mtp::rm_ref<F>;
        auto callback     = +[](void* context) {
            auto& value = *static_cast<Initializer*>(context);
            auto  state = OnceState {};
            rstd::invoke_once<void(OnceState const&)>(rstd::forward<F>(value), state);
        };
        auto* context = const_cast<void*>(static_cast<void const*>(rstd::addressof(initializer)));
        m_inner.call(context, callback);
    }

    auto is_completed() const noexcept -> bool { return m_inner.is_completed(); }

    void wait() const {
        if (! m_inner.is_completed()) m_inner.wait();
    }

    void wait_force() const {
        if (! m_inner.is_completed()) m_inner.wait();
    }
};

export template<typename T>
class OnceLock {
    mutable Once                m_once;
    mutable mem::MaybeUninit<T> m_value;

    auto value_ref() const noexcept -> ref<T> { return ref<T>::from_raw_parts(m_value.as_ptr()); }

    auto value_mut() noexcept -> mut_ref<T> {
        return mut_ref<T>::from_raw_parts(m_value.as_mut_ptr());
    }

public:
    constexpr OnceLock() noexcept: m_once(), m_value(mem::MaybeUninit<T>::uninit()) {}

    static constexpr auto make() noexcept -> OnceLock { return {}; }

    OnceLock(OnceLock const&)                    = delete;
    auto operator=(OnceLock const&) -> OnceLock& = delete;
    OnceLock(OnceLock&&)                         = delete;
    auto operator=(OnceLock&&) -> OnceLock&      = delete;

    ~OnceLock() {
        if (m_once.exclusive_state() == once_detail::ExclusiveState::Complete) {
            m_value.assume_init_drop();
        }
    }

    auto get() const& noexcept [[clang::lifetimebound]] -> Option<ref<T>> {
        if (! m_once.is_completed()) return None();
        return Some(value_ref());
    }

    auto get() const&& -> Option<ref<T>> = delete;

    auto get_mut() & noexcept [[clang::lifetimebound]] -> Option<mut_ref<T>> {
        if (m_once.exclusive_state() != once_detail::ExclusiveState::Complete) return None();
        return Some(value_mut());
    }

    auto set(T value) const -> Result<empty, T> {
        bool inserted = false;
        m_once.call_once_force([this, &value, &inserted](OnceState const&) {
            m_value.write(rstd::move(value));
            inserted = true;
        });

        if (inserted) return Ok(empty {});
        return Err(rstd::move(value));
    }

    template<typename F>
        requires requires(F&& initializer) {
            rstd::invoke_once<T()>(rstd::forward<F>(initializer));
        }
    auto get_or_init(F&& initializer) const& [[clang::lifetimebound]] -> ref<T> {
        if (m_once.is_completed()) return value_ref();

        m_once.call_once_force([this, &initializer](OnceState const&) {
            m_value.write(rstd::invoke_once<T()>(rstd::forward<F>(initializer)));
        });
        return value_ref();
    }

    template<typename F>
    auto get_or_init(F&&) const&& -> ref<T> = delete;

    auto wait() const& [[clang::lifetimebound]] -> ref<T> {
        m_once.wait_force();
        return value_ref();
    }

    auto wait() const&& -> ref<T> = delete;

    auto take() & -> Option<T> {
        if (m_once.exclusive_state() != once_detail::ExclusiveState::Complete) return None();

        auto value = T(rstd::move(m_value.assume_init_mut()));
        m_value.assume_init_drop();
        m_once.set_exclusive_state(once_detail::ExclusiveState::Incomplete);
        return Some(rstd::move(value));
    }

    auto into_inner() && -> Option<T> { return take(); }
};

export template<typename T, typename F>
class LazyLock {
    template<typename, typename>
    friend class LazyLock;

    union Storage {
        F initializer;
        T value;

        template<typename Init>
        constexpr explicit Storage(Init&& initializer)
            : initializer(rstd::forward<Init>(initializer)) {}

        ~Storage() {}
    };

    mutable Once    m_once;
    mutable Storage m_storage;

    template<typename Init>
    constexpr explicit LazyLock(Init&& initializer)
        : m_once(), m_storage(rstd::forward<Init>(initializer)) {}

    void initialize() const
        requires Impled<F, FnOnce<T()>>
    {
        struct InitializerDrop {
            F* value;

            ~InitializerDrop() {
                if (value != nullptr) rstd::destroy_at(value);
            }
        };

        auto initializer_drop = InitializerDrop { rstd::addressof(m_storage.initializer) };
        auto value            = rstd::as<FnOnce<T()>>(m_storage.initializer).call_once();
        rstd::destroy_at(rstd::addressof(m_storage.initializer));
        initializer_drop.value = nullptr;
        rstd::construct_at(rstd::addressof(m_storage.value), rstd::move(value));
    }

    auto value_ref() const noexcept -> ref<T> {
        return ref<T>::from_raw_parts(rstd::addressof(m_storage.value));
    }

    auto value_mut() noexcept -> mut_ref<T> {
        return mut_ref<T>::from_raw_parts(rstd::addressof(m_storage.value));
    }

public:
    USE_TRAIT(LazyLock)

    template<typename Init>
    static constexpr auto make(Init&& initializer) -> LazyLock<T, mtp::decay<Init>> {
        return LazyLock<T, mtp::decay<Init>>(rstd::forward<Init>(initializer));
    }

    LazyLock(LazyLock const&)                    = delete;
    auto operator=(LazyLock const&) -> LazyLock& = delete;
    LazyLock(LazyLock&&)                         = delete;
    auto operator=(LazyLock&&) -> LazyLock&      = delete;

    ~LazyLock() {
        auto const state = m_once.exclusive_state();
        if (state == once_detail::ExclusiveState::Incomplete) {
            rstd::destroy_at(rstd::addressof(m_storage.initializer));
        } else if (state == once_detail::ExclusiveState::Complete) {
            rstd::destroy_at(rstd::addressof(m_storage.value));
        }
    }

    auto get() const& noexcept [[clang::lifetimebound]] -> Option<ref<T>> {
        if (! m_once.is_completed()) return None();
        return Some(value_ref());
    }

    auto get() const&& -> Option<ref<T>> = delete;

    auto get_mut() & noexcept [[clang::lifetimebound]] -> Option<mut_ref<T>> {
        if (m_once.exclusive_state() != once_detail::ExclusiveState::Complete) return None();
        return Some(value_mut());
    }

    auto force() const& [[clang::lifetimebound]] -> ref<T>
        requires Impled<F, FnOnce<T()>>
    {
        m_once.call_once_force([this](OnceState const&) {
            initialize();
        });
        return value_ref();
    }

    auto force() const&& -> ref<T> = delete;

    auto force_mut() & [[clang::lifetimebound]] -> mut_ref<T>
        requires Impled<F, FnOnce<T()>>
    {
        static_cast<LazyLock const&>(*this).force();
        return value_mut();
    }
};
} // namespace rstd::sync

namespace rstd
{
template<typename T, typename F>
struct Impl<ops::Deref, sync::LazyLock<T, F>> : ImplBase<sync::LazyLock<T, F>> {
    using Target = T;

    auto deref() const noexcept -> ref<Target> { return this->self().force(); }
};

template<typename T, typename F>
struct Impl<ops::DerefMut, sync::LazyLock<T, F>> : ImplBase<sync::LazyLock<T, F>> {
    auto deref_mut() noexcept -> mut_ref<ops::deref_target_t<sync::LazyLock<T, F>>> {
        return this->self().force_mut();
    }
};
} // namespace rstd
