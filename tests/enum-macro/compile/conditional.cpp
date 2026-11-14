import rstd.core;

#if 1
namespace outer::inner
{
[[pmacro::invoke("rstd::Enum",
                 enum Selected {
                     [[default]] Enabled,
                 })]];
}
#else
[[pmacro::invoke("rstd::Enum", not an enum declaration)]];
#endif

static_assert(outer::inner::Selected().is_Enabled());
