#include <WiFiUdp.h>
#include "support.h"

#define OSWATCH_RESET_TIME 60

#define SYSLOG_SERVER "192.168.0.106"
#define SYSLOG_PORT 514

#define DEVICE_HOSTNAME "NodeMCU"
#define APP_NAME "Roller0"


WiFiUDP udpClient;
Syslog * syslog = NULL;

void SyslogInit() {
    snprintf_P(my_hostname, sizeof(my_hostname) - 1, "wemos-%d", ESP.getChipId() & 0x1FFF);
    syslog = new Syslog(udpClient, SYSLOG_SERVER, SYSLOG_PORT, my_hostname, APP_NAME);
    if (syslog) {
        syslog->SetGetStrDateAndTime(GetUtcDateAndTime);
    }
}
void SysLogFlush() {
    udpClient.flush();
}

Ticker tickerOSWatch;
static unsigned long oswatch_last_loop_time;
byte oswatch_blocked_loop = 0;

void OsWatchTicker() {
    unsigned long t = millis();
    unsigned long last_run = abs(t - oswatch_last_loop_time);

    if (last_run >= (OSWATCH_RESET_TIME * 1000)) {
        RtcSettings.oswatch_blocked_loop = 1;
        RtcSettingsSave();
        ESP.reset();  // hard reset
    }
}

void OsWatchInit() {
    oswatch_blocked_loop = RtcSettings.oswatch_blocked_loop;
    RtcSettings.oswatch_blocked_loop = 0;
    oswatch_last_loop_time = millis();
    tickerOSWatch.attach_ms(((OSWATCH_RESET_TIME / 3) * 1000), OsWatchTicker);
}

void OsWatchLoop() {
    oswatch_last_loop_time = millis();
}
