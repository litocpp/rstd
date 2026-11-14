import rstd.serde;
struct [[pmacro::derive("serde::derive"),
         pmacro::helper("serde::derive", variants, tag("type"), tag("kind"))]] Invalid {};
