#ifndef GKD_APP_BATTERY_H
#define GKD_APP_BATTERY_H

/* Native A telemetry adapter.  It deliberately has no PMIC/I2C fallback. */
struct gkd_app_battery {
    int percent;
    int external_power;
    int voltage_mv;
};

/* Reads voltage_now and usb/online, deriving percent exclusively from the
 * caller-supplied accepted 0/25/50/75/100 voltage curve. */
int gkd_app_battery_read(struct gkd_app_battery *out,
                         const unsigned curve_mv[5]);

/* Maps a verified percentage to the four existing gkd350 battery LEDs and
 * writes their brightness files. thresholds must be three increasing 1..99 values. */
int gkd_app_battery_led(const struct gkd_app_battery *battery,
                        const unsigned thresholds[3]);

/* Pure, opt-in voltage conversion. curve_mv is the caller-configured voltage
 * for 0, 25, 50, 75 and 100 percent, strictly increasing. */
int gkd_app_battery_percent_from_voltage(int voltage_mv,
                                         const unsigned curve_mv[5],
                                         int *percent);

#endif
