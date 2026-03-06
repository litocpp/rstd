export module rstd:forward;
import rstd.alloc;

namespace rstd_alloc = alloc;

namespace rstd
{

// we can't use export namespace xxx = mmm;
// as it's required `export import rstd.alloc;`

// TODO: gcc bug
// we can't use export using namespace xxx;

export namespace boxed
{
using rstd_alloc::boxed::Box;
}

export namespace rc
{
using rstd_alloc::rc::allocate_make_rc;
using rstd_alloc::rc::make_rc;
using rstd_alloc::rc::Rc;
} // namespace rc

export namespace string
{
using rstd_alloc::string::String;
using rstd_alloc::string::ToString;
}
export namespace vec
{
using rstd_alloc::vec::Vec;
}

// export namespace borrow = rstd_alloc::borrow;
// export namespace rstd_alloc::fmt;
// export namespace rstd_alloc::format;
// export namespace rstd_alloc::slice;
// export namespace str_   = rstd_alloc::str_;
} // namespace rstd
