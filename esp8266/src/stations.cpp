#include "stations.h"
#include "ccronexpr/ccronexpr.h"
#include "log.h"
#include <EEPROM.h>
#include "Settings.h"

namespace sprinkler_controller
{

  static uint64_t lastMillis = 0;

  static time_t now;

  // forward decl
  static void enable_ics();
  static void disable_ics();
  static void set_stations_status(uint8_t *status, uint8_t bytes_count);
  static void report_status(const Station &station);
  static time_t get_next_station_start(const char *cron, const time_t date);
  static uint8_t get_station_id(const char *topic);
  static int index_of(const char *str, const char *findstr);
  static bool starts_with(const char *start_str, const char *str);
  static void substr(const char s[], char sub[], int p, int l);
  static void substr(const char s[], char sub[], int p);

  void Station::start(time_t start, long dur = 0)
  {
    this->started = start;
    this->is_active = true;
    if (dur > 0)
    {
      this->active_duration = dur > MAX_DURATION ? MAX_DURATION : dur;
    }
    else
    {
      this->active_duration = this->config_duration;
    }

    uint8_t arr[STATION_CHANNELS] = {};
    for (int i = 0; i < STATION_CHANNELS; i++)
    {
      uint8_t part = ((this->id - 1) / STATION_CHANNELS);
      if (part == i)
      {
        arr[i] = 1 << (this->pin_on);
        break;
      }
    }
    set_stations_status(arr, STATION_CHANNELS);
    report_log("[%lld] Started station %d. Started = %lld, Duration[active] = %ld", now, this->id, this->started, this->active_duration);

    report_status(*this);
  }

  void Station::stop()
  {
    report_log("[%lld] Stopping station %d. Started = %lld, Duration[active] = %ld, Elapsed = %lld", now, this->id, this->started, this->active_duration, now - this->started);

    this->started = 0;
    this->is_active = false;
    this->active_duration = 0;

    uint8_t arr[STATION_CHANNELS] = {};
    for (int i = 0; i < STATION_CHANNELS; i++)
    {
      uint8_t part = (this->id - 1) / (STATION_CHANNELS);
      if (part == i)
      {
        arr[i] = 1 << (this->pin_off);
        break;
      }
    }
    set_stations_status(arr, STATION_CHANNELS);
    report_status(*this);
  }

  void Station::to_string(char *s)
  {
    sprintf(s, "Station[%d] { is_active: %d, started: %lld, duration[active]: %ld, duration[config]: %ld, cron: '%s' }\n", id, is_active, started, active_duration, config_duration, cron);
  }

  int Station::to_json(int offset, char *buf)
  {
    offset += sprintf(buf + offset, "{\"id\": %d,", id);
    offset += sprintf(buf + offset, "\"is_active\": %d,", is_active);
    offset += sprintf(buf + offset, "\"started\": %lld,", started);
    offset += sprintf(buf + offset, "\"act_dur\": %ld,", active_duration);
    offset += sprintf(buf + offset, "\"conf_dur\": %ld,", config_duration);
    offset += sprintf(buf + offset, "\"cron\": \"%s\"}", cron);
    return offset;
  }

  void StationController::mqttcallback(char *topic, uint8_t *payload, unsigned int length)
  {
    writeLog("MQTT Message arrived [%s]\n", topic);

    char payload_str[length + 1];
    memcpy(payload_str, payload, length);
    payload_str[length] = '\0';

    if (starts_with("lawn-irrigation/interface-mode/set", topic))
    {
      process_topic_mode_set(payload_str, length); // retained message
    }
    else if (starts_with("lawn-irrigation/enabled/set", topic))
    {
      process_topic_enabled_set(payload_str, length); // retained message
    }
    else if (starts_with("lawn-irrigation/station", topic))
    {
      Station *station = get_station_from_topic(topic);
      if (station != NULL)
      {
        if (m_interface_mode && index_of(topic, "set") > 0)
        {
          process_topic_station_set(*station, payload_str, length);
        }
        else if (m_interface_mode && index_of(topic, "state") > 0)
        {
          process_topic_station_state(*station);
        }
        else if (index_of(topic, "config") > 0)
        {
          process_topic_station_config(*station, payload_str, length); // retained message
        }
      }
    }
  }

  time_t StationController::time_now()
  {
    return m_time_client->getEpochTime();
  }

  void StationController::init(NTPClient *time_client, Settings *settings)
  {
    if (m_initialized)
      return;
    writeLog("StationController Initializing...\n");

    m_time_client = time_client;
    m_settings = settings;

    digitalWrite(ENABLE_ICS_PIN, LOW);
    pinMode(ENABLE_ICS_PIN, OUTPUT);

    load();

    int retries = 5;
    while (!m_time_client->forceUpdate() && retries-- > 0)
      ;

    mqttcli::init(this);

    time_t now = time_now();
    report_log("### Started at: '%lld'###\n", now);

    print_state();
    m_initialized = true;
  }

  void StationController::loop()
  {
    if (m_initialized)
      return;
    m_time_client->update();
    mqttcli::loop();
    web_socket::loop();
    // process station events every 30 seconds
    if (millis() - lastMillis >= 1 * 1000UL)
    {
      lastMillis = millis();

      process_station_event();
      next_station_event();
    }
  }

  void StationController::set_interface_mode(bool mode)
  {
    m_interface_mode = mode;

    save();

    report_interface_mode_state();
  }

  Station *StationController::get_station(uint8_t id)
  {
    if (!m_initialized)
      return NULL;
    if (id >= 0 && id < NUM_STATIONS)
    {
      return &(m_stations[id]);
    }
    return NULL;
  }

  // TOPIC format: lawn-irrigation/station#/set
  Station *StationController::get_station_from_topic(const char *topic)
  {
    uint8_t station_id = get_station_id(topic);
    return get_station(station_id - 1);
  }

  void StationController::check_stop_stations()
  {
    for (int i = 0; i < NUM_STATIONS; i++)
    {
      Station &station = this->m_stations[i];
      now = time_now();
      if (station.is_active == true)
      {
        if (((now - station.started) > station.active_duration))
        {
          station.stop();
          report_status(station);
        }
      }
    }
  }

  StationEvent StationController::next_station_event()
  {
    StationEvent next_event;

    for (int i = 0; i < NUM_STATIONS; i++)
    {
      Station &station = m_stations[i];

      if (station.is_active == true)
      {
        time_t t = station.started + station.active_duration;
        if (next_event.time == 0 || next_event.time > t)
        {
          next_event.id = station.id;
          next_event.time = t;
          next_event.type = EventType::STOP;
        }
      }
      else
      {
        if (strlen(station.cron) > 0)
        {
          time_t t = get_next_station_start(station.cron, time_now());
          if (next_event.time == 0 || next_event.time > t)
          {
            next_event.id = station.id;
            next_event.time = t;
            next_event.duration = station.config_duration;
            next_event.type = EventType::START;
          }
        }
      }
    }

    if (next_event.id != m_station_event.id || next_event.type != m_station_event.type)
    {
      m_station_event = next_event;
      save();
      if (next_event.type == NOOP)
      {
        writeLog("No next events\n");
      }
      else
      {
        char msg[100];
        next_event.to_string(msg);
        writeLog("Next Station Event: %s\n", msg);
      }
    }
    return next_event;
  }

  void StationController::process_station_event()
  {
    check_stop_stations();

    if (m_station_event.id == -1)
      return;

    if (m_station_event.type == START)
    {
      time_t now = time_now();
      if (now > m_station_event.time + 30)
      {
        report_log("[%lld] Scheduled START event out-of-sync with the system time...\nScheduled: '%lld' vs Now: '%lld' \nSkipping event!", now, m_station_event.time, now);
      }
      else if (now < (m_station_event.time - 30))
      {
        if (!m_interface_mode)
        {
          report_log("[%lld] Nothing to do yet...!", now);
        }
      }
      else
      {
        if (m_enabled)
        {
          Station &st = m_stations[m_station_event.id - 1];

          report_log("[%lld] Starting station %d. Duration = %ld", now, st.id, m_station_event.duration);

          st.start(now, m_station_event.duration);
          report_status(st);

          save();
        }
        else
        {
          report_log("[%lld] Skipping station START event since irrigation is disabled.", now);
        }
      }
    }
  }

  void StationController::process_topic_enabled_set(const char *payload_str, uint32_t length)
  {
    writeLog("Processing topic 'lawn-irrigation/enabled/set'...\n");

    m_enabled = strcmp(payload_str, "on") == 0;

    writeLog("Topic 'lawn-irrigation/enabled/set' done.\n");
  }

  void StationController::set_station(Station &station, EventType event, long dur)
  {
    switch (event)
    {
    case NOOP:
      break;
    case START:
      station.start(time_now(), dur);
      break;
    case STOP:
      station.stop();
      break;
    }
    
    save();
  }

  void StationController::process_topic_station_set(Station &station, const char *payload_str, uint32_t length)
  {
    writeLog("Processing topic 'lawn-irrigation/station/set'...\n");

    // get op and duration
    if (starts_with("on", payload_str))
    {
      char dur[20] = {0};
      substr(payload_str, dur, index_of(payload_str, "|") + 1);
 
      set_station(station, START, atoi(dur)); 
    }
    else if (starts_with("off", payload_str))
    {
      set_station(station, STOP, 0);
    } 

    writeLog("Topic 'lawn-irrigation/station/set' done.\n");
  }

  void StationController::process_topic_station_state(Station &station)
  {
    writeLog("Processing topic 'lawn-irrigation/station/state'...\n");

    report_status(station);

    writeLog("Topic 'lawn-irrigation/station/state' done.\n");
  }

  void StationController::process_topic_mode_set(const char *payload_str, uint16_t length)
  {
    writeLog("Processing topic 'lawn-irrigation/interface-mode/set'...\n");

    set_interface_mode(strncmp("on", payload_str, 2) == 0);

    writeLog("Topic 'lawn-irrigation/interface-mode/set' done.\n");
  }

  void StationController::process_topic_station_config(Station &station, const char *payload_str, uint32_t length)
  {
    writeLog("Processing topic 'lawn-irrigation/station/config'...\n");

    // get mode
    int sep_index = index_of(payload_str, "|");

    substr(payload_str, station.cron, 0, sep_index);
    char dur[10] = {0};
    substr(payload_str, dur, sep_index + 1);

    station.config_duration = atoi(dur);

    save();

    writeLog("Topic 'lawn-irrigation/station/config' done.\n");
  }

  void StationController::report_interface_mode_state()
  {
    mqttcli::publish("lawn-irrigation/interface-mode/state", m_interface_mode ? "on" : "off", false);
  }

  void StationController::load()
  {
    int addr = m_settings->getSprinklerStart();

    writeLog(" EEPROM adr %d\n",EEPROM.read(addr));

    if (EEPROM.read(addr) == EEPROM_MARKER)
    {
      writeLog("Loading state from EEPROM...");

      addr += 1;

      EEPROM.get(addr, m_station_event);
      addr += sizeof(m_station_event);

      for (int i = 0; i < NUM_STATIONS; i++)
      {
        EEPROM.get(addr, m_stations[i]);
        addr += sizeof(Station);
      }

      writeLog("done.\n");
    }
    else{
      writeLog("Wrong marker...\n"); 
    }
  }

  void StationController::save()
  {
    writeLog("Saving state to EEPROM...\n");

    // return;

    int addr = m_settings->getSprinklerStart();

    writeLog(" EEPROM adr %d\n",addr);

    EEPROM.write(addr, EEPROM_MARKER);
    addr += 1;

    EEPROM.put(addr, m_station_event);
    addr += sizeof(m_station_event);

    for (int i = 0; i < NUM_STATIONS; i++)
    {
      EEPROM.put(addr, m_stations[i]);
      addr += sizeof(Station);
    }

    EEPROM.commit();

    writeLog("done.\n");
  }

  void StationController::print_state()
  {
    writeLog("################################\nSystem is %s\nInterface mode = %s\n\n", m_enabled ? "ENABLED" : "DISABLED", m_interface_mode ? "ON" : "OFF");
    char msg[200];
    m_station_event.to_string(msg);
    writeLog(msg);
    for (int i = 0; i < NUM_STATIONS; i++)
    {
      memset(&msg, 0, sizeof(msg));
      m_stations[i].to_string(msg);
      writeLog(msg);
    }
    writeLog("################################\n");
  }

  void StationController::to_json(char *msg)
  {
    int offset = 0;

    offset += sprintf(msg + offset, "{\"next_event\":\"");
    offset += m_station_event.to_string(msg + offset);
    offset += sprintf(msg + offset, "\", \"stations\": [");

    for (int i = 0; i < NUM_STATIONS; i++)
    {
      if (i > 0)
      {
        offset += sprintf(msg + offset, ", "); // Add a comma between stations
      }
      offset = m_stations[i].to_json(offset, msg);
    }
    offset += sprintf(msg + offset, "]}"); // Close the stations array

    msg[offset] = '\0';
  }

  static void set_shift_register(uint8_t value)
  {
    shiftOut(SR_SERIAL_INPUT, SR_CLK, MSBFIRST, value);

    digitalWrite(SR_R_CLK, LOW);
    digitalWrite(SR_R_CLK, HIGH);
    digitalWrite(SR_R_CLK, LOW);
  }

  static void enable_ics()
  {
    // TODO: check which pin we can use to control the ICS
    //       https://randomnerdtutorials.com/esp8266-pinout-reference-gpios/

    // setup
    // use RX pin as GPIO
    pinMode(SR_SERIAL_INPUT, OUTPUT);
    digitalWrite(SR_SERIAL_INPUT, LOW);

    pinMode(ENABLE_L293_PIN, OUTPUT);
    digitalWrite(ENABLE_L293_PIN, LOW);

    pinMode(SR_OUTPUT_ENABLED, OUTPUT);
    digitalWrite(SR_OUTPUT_ENABLED, HIGH);

    pinMode(SR_CLK, OUTPUT);
    digitalWrite(SR_CLK, LOW);

    pinMode(SR_R_CLK, OUTPUT);
    digitalWrite(SR_R_CLK, LOW);

    // enable
    digitalWrite(ENABLE_ICS_PIN, HIGH);
  }

  static void disable_ics()
  {
    // disable
    digitalWrite(ENABLE_ICS_PIN, LOW);

    // float pins
    pinMode(SR_SERIAL_INPUT, INPUT);
    pinMode(ENABLE_L293_PIN, INPUT);
    pinMode(SR_OUTPUT_ENABLED, INPUT);
    pinMode(SR_CLK, INPUT);
    pinMode(SR_R_CLK, INPUT);
  }

  static void set_stations_status(uint8_t *status, uint8_t bytes_count)
  {
    if (bytes_count <= 1)
      return;

    enable_ics();

    for (int i = bytes_count - 1; i == 0; i--)
    { // send in reverse order
      set_shift_register(status[i]);
    }

    digitalWrite(SR_OUTPUT_ENABLED, LOW);
    delay(50);
    digitalWrite(ENABLE_L293_PIN, HIGH);
    delay(500);
    digitalWrite(ENABLE_L293_PIN, LOW);

    digitalWrite(SR_OUTPUT_ENABLED, HIGH);

    disable_ics();
  }

  static time_t get_next_station_start(const char *cron, time_t date)
  {
    cron_expr ce;
    const char *err = NULL;
    cron_parse_expr(cron, &ce, &err);

    if (err != NULL)
    {
      writeLog("Error while parsing the CRON expr - %s\n", err);
    }

    return cron_next(&ce, date);
  }

  static void report_status(const Station &station)
  {
    char buf[64];
    sprintf(buf, "lawn-irrigation/station%d/state", station.id);
    mqttcli::publish(buf, station.is_active ? "on" : "off", false);
    web_socket::notifyClients();
  }

  // assuming that the station ID is a single digit
  static uint8_t get_station_id(const char *topic)
  {
    char *found = strstr(topic, "/station");
    char *pos = found ? found + 8 : NULL;
    if (pos != NULL)
    {
      return (*pos) - '0';
    }

    return 0;
  }

  static int index_of(const char *str, const char *findstr)
  {
    char *pos = strstr(str, findstr);
    return pos ? pos - str : -1;
  }

  static bool starts_with(const char *start_str, const char *str)
  {
    return strncmp(start_str, str, strlen(start_str)) == 0;
  }

  static void substr(const char s[], char sub[], int p, int l)
  {
    int c = 0;

    while (c < l)
    {
      sub[c] = s[p + c];
      c++;
    }
    sub[c] = '\0';
  }

  static void substr(const char s[], char sub[], int p)
  {
    int c = 0;

    char ch;

    do
    {
      ch = s[p + c];
      sub[c] = ch;
      c++;
    } while (ch != '\0');

    sub[c] = '\0';
  }

} // namespace sprinkler_controller
