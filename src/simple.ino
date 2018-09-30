#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <ESP8266httpUpdate.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266Ping.h>
#include <PubSubClient.h>                   // MQTT
#include "support.h"
#include "settings.h"
#include <CheapStepper.h>
#include "FS.h"

CheapStepper *BlindStepper = NULL;

const char* update_path = "/WebFirmwareUpgrade";
const char* update_username = "admin";
const char* update_password = "espP@ssw0rd";

const char* ssid = "indebuurt1";
const char* password = "VnsqrtnrsddbrN";
char my_hostname[33] = {};

WiFiManager wifiManager;

ESP8266WebServer web_server(80);
ESP8266HTTPUpdateServer httpUpdater;

WiFiClient EspClient;
PubSubClient MqttClient(EspClient);

static int min_position = 0;
static int max_position = 50000;
static int current_position  = min_position;

#define settings_file "/settings.txt"
enum calibrate_modes { cmNone=0, cmClose, cmOpen, cmReset };
volatile static bool need_save_settings = false;

void stop_calibration(calibrate_modes calibrate_mode);

volatile static int step_left_last = 0;
volatile static bool run = false;


void
file_save_settings() {
    File f = SPIFFS.open(settings_file, "w");
    if (f) {
	SYSLOG(LOG_INFO, "MIN(opened) position %d, MAX(closed) position %d, current position %d", min_position, max_position, get_current_position());
        f.printf("%d %d %d %d\n", min_position, max_position, current_position, BlindStepper ? BlindStepper->getSeqN(): 0);
        f.close();
    }  else {
        SYSLOG(LOG_ERR, "File %s open for wring error", settings_file);
    }
}

void
file_load_settings() {
    if (SPIFFS.exists(settings_file)) {
        File f = SPIFFS.open(settings_file, "r");
        if (f) {
            String s = f.readStringUntil('\n');
            int tmp_min_position, tmp_max_position, tmp_current_position, tmp_microstep_position;
            if (sscanf( s.c_str(), "%d %d %d %d",  &tmp_min_position, &tmp_max_position, &tmp_current_position, &tmp_microstep_position) == 4) {
                min_position =  tmp_min_position;
                max_position = tmp_max_position;
                current_position = tmp_current_position;
                if (BlindStepper)
                    BlindStepper->setSeqN(tmp_microstep_position);
            } else {
                SYSLOG(LOG_ERR, "File %s invalid data format", settings_file);
            }
            f.close();
        } else {
            SYSLOG(LOG_ERR, "File %s open for reading error", settings_file);
        }
    } else {
        SYSLOG(LOG_ERR, "File %s not found", settings_file);
    }
}

void set_current_position(int steps_to, int steps_left) {
    current_position += steps_to - steps_left;
}

int
get_current_position() {
    return current_position;
}

int
set_blind_position(int pos) {
    if (pos < 0) pos = 0;
    if (pos > 100) pos = 100;
    if (!(max_position - min_position)) return(0);

    int desired_position = min_position + (1.- (float)pos/100.) * (float)(max_position - min_position);
    return desired_position - current_position;
}

int
get_blind_position() {
    if (!(max_position - min_position)) return(-1);
    return (1. - ((float)(current_position-min_position) / (float)(max_position - min_position)))*100.;
}


void setup_web_server() {

    web_server.on("/esp", HTTP_POST, [&]() {
        String firmware = web_server.arg("firmware");
        web_server.close();
        SYSLOG(LOG_INFO, "Begin update firmwate: %s", firmware.c_str());
        HTTPUpdateResult ret = ESPhttpUpdate.update(firmware, "1.0.0");
        web_server.begin();
        switch(ret) {
        case HTTP_UPDATE_FAILED:
            web_server.send (500, "text/json", "{\"result\":false,\"msg\":\"" + ESPhttpUpdate.getLastErrorString() + "\"}");
            Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
            break;
        case HTTP_UPDATE_NO_UPDATES:
            web_server.send (304, "text/json", "{\"result\":true,\"msg\":\"Update not necessary.\"}");
            Serial.println("Update not necessary.");
            break;
        case HTTP_UPDATE_OK:
            web_server.send (200, "text/json", "{\"result\":true,\"msg\":\"Update OK.\"}");
            file_save_settings();
            Serial.println("Update OK. Rebooting....");
            RtcSettingsSave();
            ESP.restart();
            break;
        }
    });

    httpUpdater.setup(&web_server, update_path, update_username, update_password);
    web_server.begin();
    MDNS.addService("http", "tcp", 80);
}

int spiffsActive = 0;

void
setup() {
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);

    Serial.begin(115200);
    Serial.println();
    Serial.println(__FILE__);
    Serial.println("++++++=======++++++");

    SettingsLoad();
    SyslogInit();

    wifiManager.setConfigPortalTimeout(180);

    WiFi.hostname(my_hostname);
    WiFi.begin(ssid, password);
    if (!wifiManager.autoConnect(ssid, password)) {
        Serial.println("failed to connect, we should reset as see if it connects");
        delay(3000);
        ESP.reset();
        delay(5000);
    }
    if (SPIFFS.begin()) {
        spiffsActive = 1;
    } else {
        if (SPIFFS.format()) {
            spiffsActive = 2;
        }
    }
    MDNS.begin(my_hostname);
    ping_sever(WiFi.gatewayIP().toString().c_str());
    OsWatchInit();
    interruptsInit();
    setup_web_server();

    SYSLOG(LOG_INFO, "=======Begin setup======");
    SYSLOG(LOG_INFO, "Restart reason: %s", ESP.getResetReason().c_str());
    SYSLOG(LOG_INFO, "IP address: %s", WiFi.localIP().toString().c_str());
    SYSLOG(LOG_INFO, "Chip ID = %08X", ESP.getChipId());
    SYSLOG(LOG_INFO, "Buil timestamp: %s", __TIMESTAMP__);
    SYSLOG(LOG_INFO, "Sketch size: %d", ESP.getSketchSize());
    SYSLOG(LOG_INFO, "Free space: %d", ESP.getFreeSketchSpace());
    SYSLOG(LOG_INFO, "Flash Chip ID: %d", ESP.getFlashChipId());
    SYSLOG(LOG_INFO, "Flash Chip Real Size: %d", ESP.getFlashChipRealSize());
    SYSLOG(LOG_INFO, "Flash Chip Size: %d", ESP.getFlashChipSize());
    SYSLOG(LOG_INFO, "Flash Chip Speed: %d", ESP.getFlashChipSpeed());
    SYSLOG(LOG_INFO, "Flash Chip Mode: %d", ESP.getFlashChipMode());

    RtcInit();

    MQTT_Reconnect();
    MQTT_send_state();
    BlindStepper = new CheapStepper(D5, D6, D7, D8);
    BlindStepper->setRpm(12);
    file_load_settings();
    SYSLOG(LOG_INFO, "=======End setup======");
    digitalWrite(LED_BUILTIN, HIGH);
}

void
ping_sever(const char * server) {
    Serial.print("Pinging ");
    Serial.print(server);
    Serial.print(": ");

    auto pingResult = Ping.ping(server, 3);

    if (pingResult) {
        Serial.print("SUCCESS! RTT = ");
        Serial.print(Ping.averageTime());
        Serial.println(" ms");
    } else {
        Serial.println("FAILED!");
    }
}

const char * mqtt_host = "192.168.0.106";
int mqtt_port = 8883;
const char * mqtt_client = my_hostname;
const char * mqtt_user = "owntracks";
const char * mqtt_pwd = "zhopa";

String position_topic;
String step_topic;
String calibrate_topic;
bool
MQTT_Reconnect(void) {
    if (WL_CONNECTED != WiFi.status()) return false;

    static uint32_t next_try_connect_time = 0;

    if (MqttClient.connected()) return true;

    if (GetUTCTime() < next_try_connect_time) return false;

    next_try_connect_time = GetUTCTime() + 10;

    MqttClient.setServer(mqtt_host, mqtt_port);
    MqttClient.setCallback(mqtt_callback);
    if (MqttClient.connect(mqtt_client, mqtt_user, mqtt_pwd)) {
        SYSLOG(LOG_INFO, "mqtt server %s:%d connected",mqtt_host, mqtt_port)
        position_topic = String("cmd/") + my_hostname + String("/POSITION");
        MqttClient.subscribe(position_topic.c_str());

        step_topic = String("cmd/") + my_hostname + String("/STEP");
        MqttClient.subscribe(step_topic.c_str());

        calibrate_topic = String("cmd/") + my_hostname + String("/CALIBRATE");
        MqttClient.subscribe(calibrate_topic.c_str());
        return true;
    } else {
        SYSLOG(LOG_ERR, "Unable connect to mqtt server %s:%d rc %d",mqtt_host, mqtt_port, MqttClient.state());
    }
    return false;
}

void move_to_position(int ipos) {

    int move = set_blind_position(ipos);
    SYSLOG(LOG_INFO, "move to %d%% position, newMoveTo %d %d",ipos, move > 0, abs(move));
    BlindStepper->newMove(move > 0, abs(move));

}


void
mqtt_callback(char* topic, byte* payload, unsigned int length) {
    payload[length] = '\0';
    SYSLOG(LOG_INFO, "TOPIC: %s PAYLOAD:%s", topic, payload);
    if (strcasecmp(topic,position_topic.c_str()) == 0) {
        const char * playload_on = "on";
        const char * playload_off = "off";
        const char * playload_stop = "stop";

        auto val = String((char*)payload);

        stop();

        if (strncasecmp((char*)payload,playload_stop,std::min(length,strlen(playload_stop))) == 0) {
            return;
        }

        int ipos = val.toInt();
        if (strncasecmp((char*)payload,playload_on,std::min(length,strlen(playload_on))) == 0) {
            ipos = 100;
        }

        if (strncasecmp((char*)payload,playload_off,std::min(length,strlen(playload_off))) == 0) {
            ipos = 0;
        }

        move_to_position(ipos);
        return;
    }
    if (strcasecmp(topic, step_topic.c_str()) == 0) {
        stop();
        auto val = String((char*)payload);
        SYSLOG(LOG_INFO, "newMoveTo %d %d",val.toInt() > 0, abs(val.toInt()));
        BlindStepper->newMove(val.toInt() > 0, abs(val.toInt()));
        return;
    }
    if (strcasecmp(topic, calibrate_topic.c_str()) == 0) {
        const char * playload_close = "close";
        const char * playload_open = "open";
        const char * playload_reset = "reset";

        stop();
        if (strncasecmp((char*)payload,playload_close,std::min(length,strlen(playload_close))) == 0) {
            stop_calibration(cmClose);
        }
        if (strncasecmp((char*)payload,playload_open,std::min(length,strlen(playload_open))) == 0) {
            stop_calibration(cmOpen);
        }
        if (strncasecmp((char*)payload,playload_reset,std::min(length,strlen(playload_reset))) == 0) {
            stop_calibration(cmReset);
        }
        file_save_settings();
        return;
    }

}

void
wifiOn() {
    WiFi.forceSleepWake();

    uint32_t startTime = GetUTCTime();;

    while ((WL_CONNECTED != WiFi.status()) && (GetUTCTime() - startTime < 10)) {
        delay(100);
    }

    if (GetUTCTime() - startTime >= 10) {
        SYSLOG(LOG_ERR, "WiFi not connected");
    }
}

void
wifiOff() {
    SysLogFlush();
    EspClient.flush();
    MqttClient.disconnect();
    auto now = GetUTCTime();
    int c = 0;
    while ((WIFI_OFF != WiFi.getMode()) && (GetUTCTime() - now < 3)) {
        if (WiFi.forceSleepBegin()) break;
        delay(100);
        c++;
    }
    SYSLOG(LOG_ERR, "WiFi mode is: %d status: %d c: %d", WiFi.getMode(), WiFi.status(), c);
    delay(100);
}


bool
MQTT_need_send_sensor() {
    static int old_blind_position = -1;
    if (old_blind_position != get_blind_position()) {
        old_blind_position = get_blind_position();
        return true;
    }
    return false;
}

void
MQTT_send_sensor(void) {
    char buffer[255] = {};

    snprintf_P(buffer, sizeof(buffer) - 1, PSTR("{\"Time\":\"%s\",\"RB\":{\"Position\": %d, \"Step\": %d}}"),
               GetDateAndTime().c_str(), get_blind_position(), get_current_position());

    String topic = String("tele/") + my_hostname + String("/SENSOR");

    if (MQTT_Reconnect()) {
        MQTT_publish(topic.c_str(),buffer);
    } else {
        SYSLOG(LOG_ERR, "MQTT not connected");
    }
}

void
every_hour(void) {
    ntpSyncTime();
    MQTT_send_state();
}


void
every_minute(void) {
    auto now = GetUTCTime();

    if (MQTT_need_send_sensor() || (now % 60 == 0)) {
        MQTT_send_sensor();
        if (now % 3600 == 0) {
            every_hour();
        }
    }

}

void
every_second(void) {
    MQTT_Reconnect();
    static int prev_blind_position = std::numeric_limits<int>::max();
    if (prev_blind_position != get_blind_position()) {
        prev_blind_position = get_blind_position();
        MQTT_send_sensor();
    }
    if (need_save_settings) {
	need_save_settings=false;
	if (!run) {
	    file_save_settings();
	}
    }
}

//##################################### interrupts ##################################
#define BLINDS_UP_PIN   D1    // pin used for blinds up switch
#define BLINDS_DOWN_PIN D2    // pin used for blinds down switch


void interruptsInit() {
    pinMode(BLINDS_UP_PIN, INPUT_PULLUP);
    pinMode(BLINDS_DOWN_PIN, INPUT_PULLUP);

    attachInterrupt(digitalPinToInterrupt(BLINDS_UP_PIN), handlePushUp, FALLING);
    attachInterrupt(digitalPinToInterrupt(BLINDS_DOWN_PIN), handlePushDown, FALLING);
}



void
stop_calibration(calibrate_modes calibrate_mode) {
    stop();
    switch (calibrate_mode) {
    case cmClose:
        SYSLOG(LOG_INFO, "[cmClose] set position %d as full closed position", get_current_position());
        max_position = get_current_position();
        break;
    case cmOpen:
        SYSLOG(LOG_INFO, "[cmOpen] set position %d as full opened position", get_current_position());
        min_position = get_current_position();
        break;
    case cmReset:
        SYSLOG(LOG_INFO, "[cmReset]set position to 0 min 0 max 0");
        current_position = 0;
        min_position = get_current_position();
        max_position = get_current_position();
        break;
    default:
        SYSLOG(LOG_INFO, "Unknown calibration mode! %d", calibrate_mode);
        break;
    }
}

volatile unsigned long upButtonPressed = 0;
volatile unsigned long downButtonPressed = 0;
volatile calibrate_modes calibrate_mode = cmNone;

void handleReleaseUp() {
    digitalRead(BLINDS_UP_PIN);
    attachInterrupt(digitalPinToInterrupt(BLINDS_UP_PIN), handlePushUp, FALLING);
    if (BlindStepper) {
        if (millis() - upButtonPressed > 20) {
            if (millis() - upButtonPressed < 5000) {
                if (calibrate_mode) {
                    stop_calibration(calibrate_mode);
                    calibrate_mode = cmNone;
                } else {
                    if (BlindStepper->getStepsLeft()) {
                        stop();
                    } else {
                        move_to_position(100);
                    }
                }
            } else {
                calibrate_mode = cmOpen;
                BlindStepper->newMove(0, 100000);
            }
        }
    }
    digitalWrite(LED_BUILTIN, HIGH);
}


void
handleReleaseDown() {
    digitalRead(BLINDS_DOWN_PIN);
    attachInterrupt(digitalPinToInterrupt(BLINDS_DOWN_PIN), handlePushDown, FALLING);
    if (BlindStepper) {
        if (millis() - downButtonPressed > 20) {
            if (millis() - downButtonPressed < 5000) {
                if (calibrate_mode) {
                    stop_calibration(calibrate_mode);
                    calibrate_mode = cmNone;
                } else {
                    if (BlindStepper->getStepsLeft()) {
                        stop();
                    } else {
                        move_to_position(0);
                    }
                }
            } else {
                calibrate_mode = cmClose;
                BlindStepper->newMove(1, 100000);
            }
        }
    }
    digitalWrite(LED_BUILTIN, HIGH);
}

void handlePushUp() {
    digitalRead(BLINDS_UP_PIN);
    attachInterrupt(digitalPinToInterrupt(BLINDS_UP_PIN), handleReleaseUp, RISING);
    digitalWrite(LED_BUILTIN, LOW);
    upButtonPressed = millis();
}


void handlePushDown() {
    digitalRead(BLINDS_DOWN_PIN);
    attachInterrupt(digitalPinToInterrupt(BLINDS_DOWN_PIN), handleReleaseDown, RISING);
    digitalWrite(LED_BUILTIN, LOW);
    downButtonPressed = millis();
}


void stop() {
    current_position =  current_position + (step_left_last - BlindStepper->getStepsLeft());
    BlindStepper->stop();
    run = false;
    step_left_last = 0;
    need_save_settings = true;
    SYSLOG(LOG_INFO, "Stop!");
    
}

void
main_loop(void) {
    static uint32_t last_utc_time = 0;
    if (last_utc_time != utc_time) {
        auto now = last_utc_time = GetUTCTime();
        every_second();
        if (now % 60 == 0) {
            every_minute();
        }
    }

    if (BlindStepper) {
        if (BlindStepper->getStepsLeft()) {
            if (! run) {
                run = true;
                SYSLOG(LOG_INFO, "Run ! %d steps left.", BlindStepper->getStepsLeft());
                step_left_last = BlindStepper->getStepsLeft();
            }
            current_position =  current_position + (step_left_last - BlindStepper->getStepsLeft());
            step_left_last = BlindStepper->getStepsLeft();
        } else {
            if (run) {
		stop();
                MQTT_send_sensor();
                SYSLOG(LOG_INFO, "Run done!");
                file_save_settings();
            }
        }
        BlindStepper->run();
    }
}

void
loop() {
    main_loop();
    OsWatchLoop();
    web_server.handleClient();
    MqttClient.loop();
}
