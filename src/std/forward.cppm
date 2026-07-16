export module rstd:forward;
import rstd.alloc;

namespace rstd_alloc = alloc;

namespace rstd
{

// we can't use export namespace xxx = mmm;
// as it's required `export import rstd.alloc;`

// TODO: gcc bug
// we can't use export using namespace xxx;

/// Smart pointer types for heap allocation.
export namespace boxed
{
/// A pointer type for heap allocation, analogous to Rust's `Box<T>`.
using rstd_alloc::boxed::Box;
} // namespace boxed

/// Reference-counted pointer types.
export namespace rc
{
using rstd_alloc::rc::allocate_make_rc;
using rstd_alloc::rc::make_rc;
using rstd_alloc::rc::RcRaw;
using rstd_alloc::rc::StoragePolicy;
using rstd_alloc::rc::Weak;
/// A single-threaded reference-counting pointer.
using rstd_alloc::rc::Rc;
} // namespace rc

/// UTF-8 encoded, growable string type.
export namespace string
{
/// A growable UTF-8 encoded string.
using rstd_alloc::string::String;
/// A trait for converting a value to a String.
using rstd_alloc::string::ToString;
} // namespace string

export namespace prelude
{
using rstd_alloc::boxed::Box;
using rstd_alloc::string::String;
using rstd_alloc::string::ToString;
using rstd_alloc::vec::Vec;
} // namespace prelude

/// A contiguous growable array type.
export namespace vec
{
/// A contiguous growable array type, analogous to Rust's `Vec<T>`.
using rstd_alloc::vec::Vec;
} // namespace vec

/// Ordered collection types.
export namespace collections
{
/// An ordered map backed by a B-tree.
using rstd_alloc::collections::BTreeMap;
/// A hash map using open addressing.
using rstd_alloc::collections::HashMap;
} // namespace collections

// export namespace borrow = rstd_alloc::borrow;
// export namespace rstd_alloc::fmt;
// export namespace rstd_alloc::format;
// export namespace rstd_alloc::slice;
// export namespace str_   = rstd_alloc::str_;
} // namespace rstd
