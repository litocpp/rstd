export module rstd.alloc:collections.vec_deque;
export import :vec;

using namespace rstd::prelude;
using ::alloc::vec::Vec;

export namespace alloc::collections
{

template<typename T>
class VecDeque {
    Vec<T> front_;
    Vec<T> back_;

public:
    auto len() const -> usize { return front_.len() + back_.len(); }
    auto is_empty() const -> bool { return front_.is_empty() && back_.is_empty(); }
    void push_back(T value) { back_.push(rstd::move(value)); }
    auto pop_front() -> Option<T> {
        if (front_.is_empty()) {
            while (auto item = back_.pop()) front_.push(rstd::move(*item));
        }
        return front_.pop();
    }
    void clear() {
        front_.clear();
        back_.clear();
    }
};

} // namespace alloc::collections
