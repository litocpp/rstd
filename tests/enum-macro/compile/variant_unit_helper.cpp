import rstd.core;

[[pmacro::invoke(
    "rstd::Enum",
    [[derive(Debug, Error)]] enum Invalid {
        [[helper("rstd::Error", variants, unit("Unit", "ignored"))]] [[error("unit")]] Unit,
    })]];
