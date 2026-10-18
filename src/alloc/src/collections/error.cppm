export module rstd.alloc:collections.error;

export namespace alloc::collections
{
enum class TryReserveError
{
    CapacityOverflow,
    AllocError
};
} // namespace alloc::collections
