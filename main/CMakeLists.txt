idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES rlc_common
    PRIV_REQUIRES rlc_base rlc_remote
)
