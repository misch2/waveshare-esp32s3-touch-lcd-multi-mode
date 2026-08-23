#include "../../../../waveshare-hodiny/WaveshareHodiny/OpenWeatherIcons.h"

extern "C" {
extern const lv_img_dsc_t clock_asset_clear_day_c;
extern const lv_img_dsc_t clock_asset_clear_night_c;
extern const lv_img_dsc_t clock_asset_mostly_clear_day_c;
extern const lv_img_dsc_t clock_asset_mostly_clear_night_c;
extern const lv_img_dsc_t clock_asset_partly_cloudy_day_c;
extern const lv_img_dsc_t clock_asset_partly_cloudy_night_c;
extern const lv_img_dsc_t clock_asset_overcast_day_c;
extern const lv_img_dsc_t clock_asset_overcast_night_c;
extern const lv_img_dsc_t clock_asset_overcast_c;
extern const lv_img_dsc_t clock_asset_drizzle_c;
extern const lv_img_dsc_t clock_asset_rain_c;
extern const lv_img_dsc_t clock_asset_sleet_c;
extern const lv_img_dsc_t clock_asset_snow_c;
extern const lv_img_dsc_t clock_asset_mist_c;
extern const lv_img_dsc_t clock_asset_thunderstorms_c;
}

// OpenWeatherIcons.h declares these with C++ linkage, so keep the public
// names identical to the upstream C++ dispatcher while reusing the actual
// generated image data compiled above.
const lv_img_dsc_t meteocons_static_clear_day = clock_asset_clear_day_c;
const lv_img_dsc_t meteocons_static_clear_night = clock_asset_clear_night_c;
const lv_img_dsc_t meteocons_static_mostly_clear_day =
    clock_asset_mostly_clear_day_c;
const lv_img_dsc_t meteocons_static_mostly_clear_night =
    clock_asset_mostly_clear_night_c;
const lv_img_dsc_t meteocons_static_partly_cloudy_day =
    clock_asset_partly_cloudy_day_c;
const lv_img_dsc_t meteocons_static_partly_cloudy_night =
    clock_asset_partly_cloudy_night_c;
const lv_img_dsc_t meteocons_static_overcast_day = clock_asset_overcast_day_c;
const lv_img_dsc_t meteocons_static_overcast_night =
    clock_asset_overcast_night_c;
const lv_img_dsc_t meteocons_static_overcast = clock_asset_overcast_c;
const lv_img_dsc_t meteocons_static_drizzle = clock_asset_drizzle_c;
const lv_img_dsc_t meteocons_static_rain = clock_asset_rain_c;
const lv_img_dsc_t meteocons_static_sleet = clock_asset_sleet_c;
const lv_img_dsc_t meteocons_static_snow = clock_asset_snow_c;
const lv_img_dsc_t meteocons_static_mist = clock_asset_mist_c;
const lv_img_dsc_t meteocons_static_thunderstorms = clock_asset_thunderstorms_c;
