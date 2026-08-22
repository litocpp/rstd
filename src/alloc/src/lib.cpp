module rstd.alloc;

using namespace rstd::prelude;

namespace alloc::vec
{

template class Vec<f64>;
template class Vec<u8>;
template class Vec<usize>;
template class Vec<alloc::ffi::CString>;
template class Vec<alloc::string::String>;

} // namespace alloc::vec
