module;
#include <utility>
export module rstd.core:option_adapter;
export import :option;
export import :result;

namespace rstd::option
{

namespace detail
{

template<typename T>
struct option_adapter_base {};

template<typename T>
struct option_adapter : option_adapter_base<T> {
    template<typename E>
    auto ok_or(E err) -> Result<T, E> {
        auto self = static_cast<Option<T>&>(*this);
        if (self.is_some()) {
            return Ok(self._get_move());
        } else {
            return Err(err);
        }
    }

    template<typename F, typename E = meta::invoke_result_t<F>>
        requires ImplementedT<FnOnce<F, E(void)>>
    auto ok_or_else(F&& err) -> Result<T, E> {
        auto self = static_cast<Option<T>&>(*this);
        if (self.is_some()) {
            return Ok(self._get_move());
        } else {
            return Err(std::move(err)());
        }
    }

private:
    friend class option_adapter_base<T>;
    template<typename U, typename E>
    static constexpr auto _transpose(Option<T>&& self) -> Result<Option<U>, E> {
        if (self.is_some()) {
            auto&& t = self.unwrap_unchecked();
            if (t.is_ok()) {
                return Ok(Some(std::move(t.unwrap_unchecked())));
            } else {
                return Err(std::move(t.unwrap_err_unchecked()));
            }
        } else {
            return Ok<Option<U>>(None());
        }
    }

    template<typename U>
    static constexpr auto _flatten(Option<T>&& self) -> Option<U> {
        if (self.is_some()) {
            return self._get_move();
        } else {
            return None();
        }
    }
};

template<typename T, typename E>
struct option_adapter_base<Result<T, E>> {
    constexpr auto transpose() -> Result<Option<T>, E> {
        return option_adapter<Result<T, E>>::template _transpose<T, E>(
            static_cast<Option<Result<T, E>>&&>(*this));
    }
};

template<typename T>
struct option_adapter_base<Option<T>> {
    constexpr auto flatten() -> Option<T> {
        return option_adapter<Option<T>>::template _flatten<T>(
            static_cast<Option<Option<T>>&&>(*this));
    }
};

} // namespace detail

} // namespace rstd::option