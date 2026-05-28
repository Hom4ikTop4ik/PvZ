/*
 * utils.c
 *
 */

#include <stdio.h>
#include "rtc.h"

// Буфер должен быть не меньше 21 байта!
void rtc_iso8601_string(char *buf, size_t buflen, RTC_HandleTypeDef *hrtc)
{
    RTC_TimeTypeDef sTime;
    RTC_DateTypeDef sDate;

    HAL_RTC_GetTime(hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(hrtc, &sDate, RTC_FORMAT_BIN);

    // 20 символов + '\0'
    snprintf(buf, buflen, "20%02d-%02d-%02dT%02d:%02d:%02dZ",
        sDate.Year, sDate.Month, sDate.Date,
        sTime.Hours, sTime.Minutes, sTime.Seconds);
}
