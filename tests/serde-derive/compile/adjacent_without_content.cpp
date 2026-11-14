import rstd.serde;
struct [[pmacro::derive("serde::derive"),
         pmacro::helper("serde::derive", variants, adjacent, tag("type"))]] Invalid {};
