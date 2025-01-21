/**
 * adapted to use a shield https://github.com/adafruit/Adafruit_Motor-Shield-v1/tree/master
 *
 *
 * This is an esp8266 project to control a lawn irrigation system using
 * solanoids for each sprinkler station. It connects to an MQTT broker on the
 * local WiFi network so that it can listen for events to turn on/off each
 * sprinkler station on the irrigation system.
 *
 * The system has two functioning modes:
 *   1. Background - The default mode which triggers each station based on it's cron schedule.
 *   2. Interface  - An interactive mode where users can trigger specific stations from
 *                   MQTT events (Usually used for ad-hoc watering or to test the spinklers).
 *
 * The mode can be selected using an MQTT event. While on Background mode, the ESP8266 is
 * in deep spleeping and a manual restart is required for the ESP8266 to restart and activate
 * the interface mode.
 *
 * WARNING: While on interafce mode, the ESP8266 is always on. This will degrade the battery life much faster.
 *
 * Stations are mutual exclusive. There can only be one station active at a time, which usually is the behavior of sprinkler irrigation systems.
 *
 * MQTT topics:
 *  Subsribe:
 *   - lawn-irrigation/station{x}/set     not retained -> payload: on|18000 ; off
 *   - lawn-irrigation/station{x}/config      retained -> payload: cron|18000
 *   - lawn-irrigation/interface-mode/set     retained -> payload: on ; off
 *   - lawn-irrigation/enabled/set            retained -> payload: on ; off
 *
 *  Publish:
 *   - lawn-irrigation/station{x}/state
 *   - lawn-irrigation/interface-mode/state
 *   - lawn-irrigation/log
 *
 * @file main.cpp
 * @author Bruno Conde
 * @brief
 * @version 0.1
 * @date 2022-09-18
 *
 * @copyright Copyright (c) 2022
 *
 */
#define DEBUG_ESP_WIFI
#include <Arduino.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h>

#include "mqttcli.h"
#include "web_socket.h"
#include "stations.h"
#include "log.h"
#include "Settings.h"
#include "html.h"
#include "templateProcessor.h"

const int DEEP_SLEEP_THRESHOLD = 3 * 60 * 60; // 3 hours in seconds

using namespace sprinkler_controller;

AsyncWebServer server(80);

DNSServer dns;

StationController stctr;
WiFiUDP ntpUDP;
NTPClient time_client(ntpUDP);
Settings settings;
bool shouldSaveConfig = false;
Station *station = nullptr;

unsigned long lastMillis = 0;

bool serverInitialized = false; 
bool disconnected = true;

void enter_deep_sleep()
{
  // Find out the next event
  time_t now = time_client.getEpochTime();
  StationEvent ev = stctr.next_station_event();

  time_t sleep_duration = DEEP_SLEEP_THRESHOLD;
  if (ev.type != EventType::NOOP && ev.time > now)
  {
    time_t new_sleep_duration = ev.time - now;
    // The maximum deep sleep time is around 3 hours. This may vary due to temperature changes.
    // ESP.deepSleepMax() provides the max deep sleep.
    // We wake up every 3 hours and go right back to sleep if no stations need to be started/stopped.
    if (new_sleep_duration <= DEEP_SLEEP_THRESHOLD)
    {
      sleep_duration = new_sleep_duration;
    }

    char msg[200] = {0};
    ev.to_string(msg);
    report_log("[%lld] Next event: %s", now, msg);
  }
  else
  {
    // There isn't a next event to process
    report_log("[%lld] No next event found!", now);
  }

  report_log("[%lld] Entering deep sleep mode for '%lld' seconds...", now, sleep_duration);

  mqttcli::disconnect();
  ESP.deepSleep(sleep_duration * 1000 * 1000); // convert from seconds to microseconds
}

void saveConfigCallback()
{
  shouldSaveConfig = true;
}

void initializeServer()
{
  serverInitialized = true;
  disconnected = false;
  writeLog("WiFi connected. IP address: %s\n", WiFi.localIP().toString().c_str()); 
  writeLog("Wi-Fi Signal Strength: %d dBm\n", WiFi.RSSI());
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            {
              AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", HTML_MAIN, templateProcessor);
              request->send(response); });
  server.onNotFound([](AsyncWebServerRequest *request)
                    { request->send(404, "text/plain", "404"); });

  sprinkler_controller::web_socket::init(&server, &stctr);
 
  time_client.setTimeOffset(settings.data.ntpOffset);
  time_client.setPoolServerName(settings.data.ntpServer);
  time_client.begin();

  writeLog("NTP Server conf: %s, offset %d\n", settings.data.ntpServer, settings.data.ntpOffset);

  stctr.init(&time_client, &settings);
  stctr.set_interface_mode(true); 
  server.begin();
}

bool init_wifi()
{
  WiFi.persistent(true); // fix wifi save bug
  WiFi.hostname(settings.data.deviceName);
  AsyncWiFiManager wm(&server, &dns);
  wm.setMinimumSignalQuality(20);
  wm.setSaveConfigCallback(saveConfigCallback);

  AsyncWiFiManagerParameter custom_mqtt_server("mqtt_server", "MQTT server", NULL, STRING_LENGTH);
  AsyncWiFiManagerParameter custom_mqtt_user("mqtt_user", "MQTT User", NULL, STRING_LENGTH);
  AsyncWiFiManagerParameter custom_mqtt_pass("mqtt_pass", "MQTT Password", NULL, STRING_LENGTH);
  AsyncWiFiManagerParameter custom_device_name("device_name", "Device Name", "Sprinkler", STRING_LENGTH);

  wm.addParameter(&custom_mqtt_server);
  wm.addParameter(&custom_mqtt_user);
  wm.addParameter(&custom_mqtt_pass);
  wm.addParameter(&custom_device_name);

  bool apRunning = wm.autoConnect("Sprinkler-AP");

  if (shouldSaveConfig)
  {
    strncpy(settings.data.mqttServer, custom_mqtt_server.getValue(), STRING_LENGTH);
    strncpy(settings.data.mqttUser, custom_mqtt_user.getValue(), STRING_LENGTH);
    strncpy(settings.data.mqttPassword, custom_mqtt_pass.getValue(), STRING_LENGTH);
    strncpy(settings.data.deviceName, custom_device_name.getValue(), STRING_LENGTH);
    settings.save();
    // ESP.restart();
  }
  if (apRunning)
  {
    initializeServer();
  }
  else
  {
    writeLog("Failed to connect to Wi-Fi or start AP. Retrying in loop...");
  }

  return apRunning;
}

void setup()
{
  setupSerial();
  settings.load();

  init_wifi(); 

  ArduinoOTA.onStart([]()
                     {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    writeLog("Start updating %s\n", type); });
  ArduinoOTA.onEnd([]()
                   { writeLog("\nEnd"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                        { writeLog("Progress: %u%%\r", (progress / (total / 100))); });
  ArduinoOTA.onError([](ota_error_t error)
                     {
    writeLog("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      writeLog("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      writeLog("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      writeLog("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      writeLog("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      writeLog("End Failed");
    }
    writeLog("\n"); });
  ArduinoOTA.begin();
}


void loop()
{
  if (!stctr.is_interface_mode())
  {
    writeLog("Interface mode switched off\n");
    // if we get here, this means that the interface mode was switched off
    stctr.check_stop_stations();
    enter_deep_sleep();
  }
  else // manually controll todo remove this
  {
    // if (millis() - lastMillis >= 20 * 1000UL)
    // {
    //   lastMillis = millis();
    //   station = stctr.get_station(0);
    //   if (station != NULL)
    //   {
    //     station->start(time_client.getEpochTime(), 5);
    //   }
    // }
    ///

    if (WiFi.status() != WL_CONNECTED)
    {
      if (!disconnected)
      {
        writeLog("WiFi disconnected. Reconnecting...\n");
        disconnected = true;
        // stctr.init(&time_client, &settings);
      }
    }
    else
    {
      if (!serverInitialized)
      {
        Serial.println("Wi-Fi connected! Initializing server...");
        initializeServer();
      }
      if (disconnected)
      {
        writeLog("WiFi reconnected\n");
        time_client.forceUpdate();
        disconnected = false;
      }
    }

    ArduinoOTA.handle();

    stctr.loop();
  }
}
