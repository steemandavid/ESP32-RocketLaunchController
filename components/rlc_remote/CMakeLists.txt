if(CONFIG_RLC_UNIT_REMOTE)
    set(REMOTE_SRCS
        "src/rlc_remote_main.c"
        "src/rlc_encoder.c"
        "src/rlc_buzzer.c"
        "src/rlc_display.c"
        "src/rlc_remote_state.c"
        "src/rlc_remote_fsm.c"
        "src/rlc_arm_switch.c"
        "src/rlc_fire_button.c"
        "src/rlc_remote_battery.c"
        "src/rlc_remote_faultinject.c"
    )
else()
    set(REMOTE_SRCS "")
endif()

idf_component_register(
    SRCS ${REMOTE_SRCS}
    INCLUDE_DIRS "include"
    REQUIRES rlc_common driver esp_timer
)
