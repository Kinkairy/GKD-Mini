/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_SETTINGS_VALUES_H
#define GKD_SETTINGS_VALUES_H
/* One owner-selected ladder shared by draft navigation and persistence. */
static const unsigned gkd_settings_sleep_minutes[] = {0U,5U,10U,15U,30U,60U};
#define GKD_SETTINGS_SLEEP_COUNT 6U
static inline int gkd_settings_sleep_valid(unsigned minutes)
{
    for(unsigned i=0;i<GKD_SETTINGS_SLEEP_COUNT;++i)
        if(gkd_settings_sleep_minutes[i]==minutes)return 1;
    return 0;
}
static inline unsigned gkd_settings_sleep_step(unsigned minutes,int direction)
{
    for(unsigned i=0;i<GKD_SETTINGS_SLEEP_COUNT;++i){
        if(gkd_settings_sleep_minutes[i]!=minutes)continue;
        if(direction<0&&i)return gkd_settings_sleep_minutes[i-1U];
        if(direction>0&&i+1U<GKD_SETTINGS_SLEEP_COUNT)return gkd_settings_sleep_minutes[i+1U];
        return minutes;
    }
    return minutes;
}
#endif
