if(CONFIG_RLC_UNIT_BASE)
    set(BASE_SRCS
        "src/rlc_base_main.c"
        "src/rlc_relay.c"
        "src/rlc_siren.c"
        "src/rlc_base_state.c"
        "src/rlc_arm_sense.c"
        "src/rlc_continuity.c"
        "src/rlc_base_battery.c"
        "src/rlc_status_update.c"
    )
else()
    set(BASE_SRCS "")
endif()

idf_component_register(
    SRCS ${BASE_SRCS}
    INCLUDE_DIRS "include"
    REQUIRES rlc_common driver esp_timer
)
