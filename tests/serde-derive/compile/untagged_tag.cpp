import rstd.serde;

struct [[pmacro::derive("serde::derive"), pmacro::helper("serde::derive", untagged, tag("kind"))]]
Invalid {};
