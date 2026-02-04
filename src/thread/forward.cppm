export module rstd.thread:forward;
export import rstd.core;

namespace rstd::thread {
template<typename T>
using Result = result::Result<T, int>;

};