if(CONFIG_RLC_UNIT_BASE)
    set(BASE_SRCS
        "src/rlc_base_main.c"
        "src/rlc_relay.c"
        "src/rlc_siren.c"
        "src/rlc_base_state.c"
    )
else()
    set(BASE_SRCS "")
endif()

idf_component_register(
    SRCS ${BASE_SRCS}
    INCLUDE_DIRS "include"
    REQUIRES rlc_common driver esp_timer
)
