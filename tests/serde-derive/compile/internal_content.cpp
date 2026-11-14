import rstd.serde;
struct [[pmacro::derive("serde::derive"),
         pmacro::helper("serde::derive", variants, internal, tag("type"), content("data"))]]
Invalid {};
