#pragma once
#ifndef _STATION_H_
#define _STATION_H_

#include <arduino.h>
#include <NTPClient.h>
#include "mqttcli.h"

// Forward declaration
class Settings;

#define NUM_STATIONS 8 //two Adafruit_Motor-Shield shiled boards can be connected together to extend number of sprinklers. need to connect pin 9 of the first 74hct595 to the pin 14 of the second 74hct595. rest pins should be connected in parralel on both boards

#define STATION_CHANNELS NUM_STATIONS / 4

#define MAX_DURATION 4 * 60 * 60

#define SR_SERIAL_INPUT D4   // 8 pin on shield
#define SR_R_CLK D5          // 12 pin on shield
#define SR_CLK D0            // 4 pin on shield , also led on the mcu board
#define SR_OUTPUT_ENABLED D3 // 7 pin on shield

#define ENABLE_ICS_PIN D6 // disable ground

#define ENABLE_L293_PIN D2 // 6,5 11,3 connect all EN pins together
// Bit positions in the 74HCT595 shift register output
#define STATION1_A 2
#define STATION1_B 3
#define STATION2_A 1
#define STATION2_B 4
#define STATION4_A 0
#define STATION4_B 6
#define STATION3_A 5
#define STATION3_B 7

#define EEPROM_MARKER 116 // EEPROM_SETTINGS_SIZE + 10;

#define EEPROM_SPRINKLER_SIZE (1 + sizeof(sprinkler_controller::StationEvent) + (sizeof(sprinkler_controller::Station) * NUM_STATIONS))


namespace sprinkler_controller
{

  /**
   * The Station structure which contains the station configuration as well as the
   * station state
   **/
  struct Station
  {
    // config
    uint8_t id;
    uint8_t pin_on;
    uint8_t pin_off;
    char cron[40];
    long config_duration; // in seconds

    // state
    bool is_active;
    time_t started;
    long active_duration; // in seconds

    void start(time_t start, long dur);
    void stop();
    void to_string(char *buf);
  };

  enum EventType
  {
    NOOP,
    START,
    STOP
  };

  struct StationEvent
  {
    int8_t id = -1;
    time_t time = 0;
    long duration = 0;
    EventType type = NOOP;

    inline void to_string(char *str)
    {
      String t_str;
      switch (type)
      {
      case NOOP:
        t_str = "NOOP";
        break;
      case START:
        t_str = "START";
        break;
      case STOP:
        t_str = "STOP";
        break;
      }
      sprintf(str, "Station:%d; Event:%s; At:%lld; Duration:%ld;\n", id, t_str.c_str(), time, duration);
    }
  };

  class StationController
  {
  public:
    StationController() = default;

    Settings *m_settings;

    void init(NTPClient *time_client, Settings *settings);
    StationEvent next_station_event();
    void process_station_event();
    void check_stop_stations();
    void loop();
    constexpr bool is_interface_mode()
    {
      return m_interface_mode;
    }
    void set_interface_mode(bool mode);

    void mqttcallback(char *topic, uint8_t *payload, unsigned int length);

    void print_state();
    Station *get_station(uint8_t id);

  private:
    NTPClient *m_time_client;
    bool m_enabled = true;
    bool m_interface_mode;
    Station m_stations[NUM_STATIONS] = {
                                        {1, STATION1_A, STATION1_B, "", 0, false, 0, 0},
                                        {2, STATION2_A, STATION2_B, "", 0, false, 0, 0},
                                        {3, STATION3_A, STATION3_B, "", 0, false, 0, 0},
                                        {4, STATION4_A, STATION4_B, "", 0, false, 0, 0},
                                        {5, STATION1_A, STATION1_B, "", 0, false, 0, 0},
                                        {6, STATION2_A, STATION2_B, "", 0, false, 0, 0},
                                        {7, STATION3_A, STATION3_B, "", 0, false, 0, 0},
                                        {8, STATION4_A, STATION4_B, "", 0, false, 0, 0}};
    StationEvent m_station_event;

    Station *get_station_from_topic(const char *topic);
    void process_topic_mode_set(const char *payload_str, uint16_t length);
    void process_topic_mode_state(const char *payload_str, uint16_t length);
    void process_topic_station_set(Station &station, const char *payload_str, uint32_t length);
    void process_topic_station_state(Station &station);
    void process_topic_station_config(Station &station, const char *payload_str, uint32_t length);
    void process_topic_enabled_set(const char *payload_str, uint32_t length);
    void report_interface_mode_state();
    void load();
    void save();
  };
  
} // namespace sprinkler_controller

#endif