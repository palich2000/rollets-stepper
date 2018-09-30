#include <time.h>
extern "C" {
#include <sntp.h>
}
#include "support.h"
#include "settings.h"

/*********************************************************************************************\
 * RTC memory
\*********************************************************************************************/

#define NTP_SERVER1            "pool.ntp.org"       // [NtpServer1] Select first NTP server by name or IP address (129.250.35.250)
#define NTP_SERVER2            "nl.pool.ntp.org"    // [NtpServer2] Select second NTP server by name or IP address (5.39.184.5)
#define NTP_SERVER3            "0.nl.pool.ntp.org"  // [NtpServer3] Select third NTP server by name or IP address (93.94.224.67)

char ntp_servers[3][33] = {};

#define RTC_MEM_VALID 0xA55A

int uptime = 0;
bool latest_uptime_flag = true;

uint32_t rtc_settings_hash = 0;

uint32_t GetRtcSettingsHash() {
    uint32_t hash = 0;
    uint8_t *bytes = (uint8_t*)&RtcSettings;

    for (uint16_t i = 0; i < sizeof(RtcSettings); i++) {
        hash += bytes[i] * (i + 1);
    }
    return hash;
}

void RtcSettingsSave() {
    if (GetRtcSettingsHash() != rtc_settings_hash) {
        RtcSettings.valid = RTC_MEM_VALID;
        ESP.rtcUserMemoryWrite(100, (uint32_t*)&RtcSettings, sizeof(RtcSettings));
        rtc_settings_hash = GetRtcSettingsHash();
    }
}

void RtcSettingsLoad() {
    ESP.rtcUserMemoryRead(100, (uint32_t*)&RtcSettings, sizeof(RtcSettings));
    if (RtcSettings.valid != RTC_MEM_VALID) {
        memset(&RtcSettings, 0, sizeof(RtcSettings));
        RtcSettings.valid = RTC_MEM_VALID;
        RtcSettingsSave();
    }
    rtc_settings_hash = GetRtcSettingsHash();
}

boolean RtcSettingsValid() {
    return (RTC_MEM_VALID == RtcSettings.valid);
}

/*******************************************************************************************/

void SettingsLoad() {
    RtcSettingsLoad();
}

/*******************************************************************************************/
#define SECS_PER_MIN  ((uint32_t)(60UL))
#define SECS_PER_HOUR ((uint32_t)(3600UL))
#define SECS_PER_DAY  ((uint32_t)(SECS_PER_HOUR * 24UL))
#define LEAP_YEAR(Y)  (((1970+Y)>0) && !((1970+Y)%4) && (((1970+Y)%100) || !((1970+Y)%400)))
#define D_MONTH3LIST "JanFebMarAprMayJunJulAugSepOctNovDec"
static const char kMonthNames[] = D_MONTH3LIST;

static const uint8_t kDaysInMonth[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }; // API starts months from 1, this array starts from 0

uint32_t utc_time = 0;
uint32_t local_time = 0;
uint32_t daylight_saving_time = 0;
uint32_t standard_time = 0;
uint32_t ntp_time = 0;
uint32_t midnight = 1451602800;
uint8_t  midnight_now = 0;

void SetUTCTime(uint32_t t) {
    utc_time = t;
}

uint32_t GetUTCTime() {
    return utc_time;
}

uint32_t MakeTime(TIME_T &tm) {
// assemble time elements into time_t
// note year argument is offset from 1970

    int i;
    uint32_t seconds;

    // seconds from 1970 till 1 jan 00:00:00 of the given year
    seconds = tm.year * (SECS_PER_DAY * 365);
    for (i = 0; i < tm.year; i++) {
        if (LEAP_YEAR(i)) {
            seconds +=  SECS_PER_DAY;   // add extra days for leap years
        }
    }

    // add days for this year, months start from 1
    for (i = 1; i < tm.month; i++) {
        if ((2 == i) && LEAP_YEAR(tm.year)) {
            seconds += SECS_PER_DAY * 29;
        } else {
            seconds += SECS_PER_DAY * kDaysInMonth[i - 1]; // monthDay array starts from 0
        }
    }
    seconds += (tm.day_of_month - 1) * SECS_PER_DAY;
    seconds += tm.hour * SECS_PER_HOUR;
    seconds += tm.minute * SECS_PER_MIN;
    seconds += tm.second;
    return seconds;
}

void BreakTime(uint32_t time_input, TIME_T &tm) {
// break the given time_input into time components
// this is a more compact version of the C library localtime function
// note that year is offset from 1970 !!!

    uint8_t year;
    uint8_t month;
    uint8_t month_length;
    uint32_t time;
    unsigned long days;

    time = time_input;
    tm.second = time % 60;
    time /= 60;                // now it is minutes
    tm.minute = time % 60;
    time /= 60;                // now it is hours
    tm.hour = time % 24;
    time /= 24;                // now it is days
    tm.day_of_week = ((time + 4) % 7) + 1;  // Sunday is day 1

    year = 0;
    days = 0;
    while((unsigned)(days += (LEAP_YEAR(year) ? 366 : 365)) <= time) {
        year++;
    }
    tm.year = year;            // year is offset from 1970

    days -= LEAP_YEAR(year) ? 366 : 365;
    time -= days;              // now it is days in this year, starting at 0
    tm.day_of_year = time;

    days = 0;
    month = 0;
    month_length = 0;
    for (month = 0; month < 12; month++) {
        if (1 == month) { // february
            if (LEAP_YEAR(year)) {
                month_length = 29;
            } else {
                month_length = 28;
            }
        } else {
            month_length = kDaysInMonth[month];
        }

        if (time >= month_length) {
            time -= month_length;
        } else {
            break;
        }
    }
    strlcpy(tm.name_of_month, kMonthNames + (month * 3), 4);
    tm.month = month + 1;      // jan is month 1
    tm.day_of_month = time + 1;         // day of month
    tm.valid = (time_input > 1451602800);  // 2016-01-01
}

int WifiGetRssiAsQuality(int rssi) {
    int quality = 0;

    if (rssi <= -100) {
        quality = 0;
    } else if (rssi >= -50) {
        quality = 100;
    } else {
        quality = 2 * (rssi + 100);
    }
    return quality;
}

String
wifiStatusJson(void) {
    char buffer[255];
    snprintf_P(buffer, sizeof(buffer), PSTR("\"Wifi\":{\"SSId\":\"%s\",\"RSSI\":%d,\"APMac\":\"%s\"}"),
               ssid, WifiGetRssiAsQuality(WiFi.RSSI()), WiFi.BSSIDstr().c_str());
    return String(buffer);
}

bool
MQTT_publish(const char * topic, const char * message) {
    if (!MqttClient.connected()) return -1;

    bool ret = MqttClient.publish(topic, message);
    if (!ret) {
        SYSLOG(LOG_ERR, "Unable publish to mqtt server. Reason:%d", MqttClient.state());
    } else {
        SYSLOG(LOG_INFO, "%s %s", topic, message);
    }
    return ret;
}



void
MQTT_send_state(void) {
    char buffer[255] = {};
    char voltage[16];
    dtostrfd((double)ESP.getVcc() / 1000, 3, voltage);
    snprintf_P(buffer, sizeof(buffer) - 1, PSTR("{\"Time\":\"%s\",\"Uptime\":%d,\"Vcc\":%s, %s}"), GetDateAndTime().c_str(), uptime, voltage, wifiStatusJson().c_str());
    String topic = String("tele/") + my_hostname + String("/STATE");
    MQTT_publish(topic.c_str(),buffer);
}

void ntpSyncTime() {
    ntp_time = sntp_get_current_timestamp();
    if (ntp_time) {
        TIME_T tmpTime;
        utc_time = ntp_time;
        BreakTime(utc_time, tmpTime);
        RtcTime.year = tmpTime.year + 1970;
    }
}

void RtcSecond() {

    byte ntpsync = 0;

    if (RtcTime.year < 2016) {
        if (WL_CONNECTED == WiFi.status()) {
            ntpsync = 1;  // Initial NTP sync
        }
    } else {
        if ((1 == RtcTime.minute) && (1 == RtcTime.second)) {
            ntpsync = 1;  // Hourly NTP sync at xx:01:01
        }
    }

    if (ntpsync) {
        ntpSyncTime();
    }

    utc_time++;
    local_time = utc_time;
    BreakTime(local_time, RtcTime);
    RtcTime.year += 1970;

    if ((2 == RtcTime.minute) && latest_uptime_flag) {
        latest_uptime_flag = false;
        uptime++;
        MQTT_send_state();
    }
    if ((3 == RtcTime.minute) && !latest_uptime_flag) {
        latest_uptime_flag = true;
    }
}

Ticker TickerRtc;

void RtcInit() {
    strlcpy(ntp_servers[0], NTP_SERVER1, sizeof(ntp_servers[0]));
    strlcpy(ntp_servers[1], NTP_SERVER2, sizeof(ntp_servers[1]));
    strlcpy(ntp_servers[2], NTP_SERVER3, sizeof(ntp_servers[2]));
    sntp_setservername(0, ntp_servers[0]);
    sntp_setservername(1, ntp_servers[1]);
    sntp_setservername(2, ntp_servers[3]);
//    for (int i=0; i < sizeof(ntp_servers)/sizeof(ntp_servers[0]); i++) {
//	ping_ntp_sever(ntp_servers[i]);
//    }
    sntp_stop();
    sntp_set_timezone(0);      // UTC time
    sntp_init();

    utc_time = 0;

    BreakTime(utc_time, RtcTime);
    TickerRtc.attach(1, RtcSecond);
}

String GetUtcDateAndTime() {
    // "2017-03-07T11:08:02" - ISO8601:2004
    char dt[21];

    TIME_T tmpTime;
    BreakTime(utc_time, tmpTime);
    tmpTime.year += 1970;

    snprintf_P(dt, sizeof(dt), PSTR("%04d/%02d/%02dT%02d:%02d:%02d"),
               tmpTime.year, tmpTime.month, tmpTime.day_of_month, tmpTime.hour, tmpTime.minute, tmpTime.second);
    return String(dt);
}

String GetDateAndTime() {
    // "2017-03-07T11:08:02" - ISO8601:2004
    char dt[21];

    snprintf_P(dt, sizeof(dt), PSTR("%04d/%02d/%02dT%02d:%02d:%02d"),
               RtcTime.year, RtcTime.month, RtcTime.day_of_month, RtcTime.hour, RtcTime.minute, RtcTime.second);
    return String(dt);
}

char* _dtostrf(double number, unsigned char prec, char *s) {
    bool negative = false;

    if (isnan(number)) {
        strcpy_P(s, PSTR("nan"));
        return s;
    }
    if (isinf(number)) {
        strcpy_P(s, PSTR("inf"));
        return s;
    }
    char decimal = '.';

    char* out = s;

    // Handle negative numbers
    if (number < 0.0) {
        negative = true;
        number = -number;
    }

    // Round correctly so that print(1.999, 2) prints as "2.00"
    // I optimized out most of the divisions
    double rounding = 2.0;
    for (uint8_t i = 0; i < prec; ++i) {
        rounding *= 10.0;
    }
    rounding = 1.0 / rounding;
    number += rounding;

    // Figure out how big our number really is
    double tenpow = 1.0;
    int digitcount = 1;
    while (number >= 10.0 * tenpow) {
        tenpow *= 10.0;
        digitcount++;
    }
    number /= tenpow;

    // Handle negative sign
    if (negative) {
        *out++ = '-';
    }

    // Print the digits, and if necessary, the decimal point
    digitcount += prec;
    int8_t digit = 0;
    while (digitcount-- > 0) {
        digit = (int8_t)number;
        if (digit > 9) {
            digit = 9; // insurance
        }
        *out++ = (char)('0' | digit);
        if ((digitcount == prec) && (prec > 0)) {
            *out++ = decimal;
        }
        number -= digit;
        number *= 10.0;
    }

    // make sure the string is terminated
    *out = 0;
    return s;
}

char* dtostrfd(double number, unsigned char prec, char *s) { // Always decimal dot
    return _dtostrf(number, prec, s);
}
