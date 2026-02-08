export module rstd.core:mem;

export import :mem.manually_drop;
export import :mem.maybe_uninit;
export import :mem.drop_guard;

export namespace rstd::mem
{
using drop_guard::DropGuard;
using manually_drop::ManuallyDrop;
using maybe_uninit::MaybeUninit;
} // namespace rstd::mem