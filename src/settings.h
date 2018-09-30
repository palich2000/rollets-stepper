#ifndef _SETTINGS_H_
#define _SETTINGS_H_

#define CMDSZ                  24           // Max number of characters in command
#define TOPSZ                  100          // Max number of characters in topic string
#define MESSZ                  405          // Max number of characters in JSON message string (4 x DS18x20 sensors)

#define SYSLOG_TIMER           600          // Seconds to restore syslog_level
#define SERIALLOG_TIMER        600          // Seconds to disable SerialLog

#define D_LOG_APPLICATION "APP: "  // Application
#define D_SYSLOG_HOST_NOT_FOUND "Syslog Host not found"
#define D_RETRY_IN "Retry in"

#define D_YEAR_MONTH_SEPARATOR "-"
#define D_MONTH_DAY_SEPARATOR "-"
#define D_DATE_TIME_SEPARATOR "T"
#define D_HOUR_MINUTE_SEPARATOR ":"
#define D_MINUTE_SECOND_SEPARATOR ":"

#define D_UNIT_SECOND "sec"

enum LoggingLevels {LOG_LEVEL_NONE, LOG_LEVEL_ERROR, LOG_LEVEL_INFO, LOG_LEVEL_DEBUG, LOG_LEVEL_DEBUG_MORE, LOG_LEVEL_ALL};

typedef struct RTCMEM {
    uint16_t      valid;                     // 000
    byte          oswatch_blocked_loop;      // 002
    uint8_t       unused;                    // 003
    int           min_position;
    int           max_position;
    int           current_position;
    int           microstep_position;
} RtcSettings_t;

RtcSettings_t RtcSettings = {};

struct TIME_T {
    uint8_t       second;
    uint8_t       minute;
    uint8_t       hour;
    uint8_t       day_of_week;               // sunday is day 1
    uint8_t       day_of_month;
    uint8_t       month;
    char          name_of_month[4];
    uint16_t      day_of_year;
    uint16_t      year;
    unsigned long valid;
} RtcTime;

extern int uptime;
extern uint32_t utc_time;

ADC_MODE(ADC_VCC);
#include <Ticker.h>                         // RTC, HLW8012, OSWatch

#endif
