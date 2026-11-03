"""SPICE cases for the mixer and outputs (core/netlists/mix.cir): tones on several voices at once,
the master volume's ends with the summers clipping, cables in the MULTI OUT jacks and mono, and the
power-on mute."""


def cases(add, h):
    sec = 0.3
    step = 3 / 96000
    run = {"pon": h.hold(15.0)}
    tones = dict(run)
    tones.update({
        "bd": h.sines([(55, 2.0, 0.0)], sec, step),
        "lt": h.sines([(130, 1.5, 0.3)], sec, step),
        "hh": h.sines([(7000, 0.8, 0.0), (11000, 0.4, 1.0)], sec, step),
        "cr": h.sines([(3100, 0.7, 0.5)], sec, step),
        "rd": h.sines([(4700, 0.7, 1.5)], sec, step),
    })
    add("mix", "mix", sec, tones)
    add("mix_vol0", "mix", sec, tones, {"volume": 0})
    loud = dict(run)
    loud.update({
        "bd": h.sines([(55, 7.0, 0.0)], sec, step),
        "sd": h.sines([(180, 5.0, 0.0), (2300, 2.0, 1.0)], sec, step),
        "ht": h.sines([(210, 4.0, 0.7)], sec, step),
    })
    add("mix_vol1_clip", "mix", sec, loud, {"volume": 1})
    add("mix_plugged_mono", "mix", sec, tones, {"plug_bd": 1, "plug_hh": 1, "plugl": 0, "volume": 0.8})
    # power on at 0.05 s: C143 starts discharged, Q82 / Q83 hold both outputs until it charges
    on = {"pon": [(0, -15.0), (0.05, -15.0), (0.05 + h.E, 15.0)],
          "bd": h.triangle(240, 3.0, 2.0), "hh": h.triangle(24, 1.0, 2.0)}
    add("mix_poweron", "mix", 2.0, on, {"volume": 1})
