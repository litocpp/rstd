import rstd.serde;
struct [[pmacro::derive("serde::derive"),
         pmacro::helper("serde::derive", variants, tag("type"), content("type"))]] Invalid {};
