idf_component_register(
    SRCS
        "main.c"
        "cli.c"
        "hw_relay.c"
        "hw_continuity.c"
        "hw_battery.c"
        "hw_inputs.c"
        "hw_siren.c"
        "hw_rgb_led.c"
        "hw_fire_timer.c"
        # The PRODUCTION band classifier, compiled in directly rather than
        # copied (2026-09-15). This bench tool had drifted two threshold
        # rebases behind the real firmware (pre-v1.29 66 mV / 1500 mV values
        # at 12 dB, its own CONT_BAND_* enum, no SUSPECT) while the main FSD
        # moved to 0 dB, the 217 ohm sense branch and four bands. A copy is
        # what caused that; pointing at the production source means the tool
        # and the firmware can never disagree again. rlc_config.h requires
        # rlc_secrets.h (bug #20) — run ./tools/gen-secrets.sh at the repo
        # root if a fresh clone is missing it.
        "../../components/rlc_common/src/rlc_continuity_class.c"
    INCLUDE_DIRS
        "."
        "../../components/rlc_common/include"
)
