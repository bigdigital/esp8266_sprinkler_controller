#include "stations.h"
#include "ccronexpr/ccronexpr.h"
#include "log.h"
#include <EEPROM.h>

namespace sprinkler_controller {

static uint8_t EEPROM_MARKER = 111;

static int EEPROM_SIZE = sizeof(EEPROM_MARKER) + sizeof(StationEvent) + (sizeof(Station) * 3);

// forward decl
static void setup_sr();
static void set_stations_status(uint8_t status, uint8_t enable_pin);
static void report_status(const Station &station);
static time_t get_next_station_start(const char *cron, const time_t date);
static uint8_t get_station_id(const char *topic);
static int index_of(const char *str, const char *findstr);
static bool starts_with(const char* start_str, const char* str);
static void substr(char s[], char sub[], int p, int l);
static void substr(char s[], char sub[], int p);

void Station::start(time_t start, long dur = 0) {
  DEBUG_PRINT("Starting station [");
  DEBUG_PRINT(this->id);
  DEBUG_PRINTLN("]");
  this->started = start;
  this->is_active = true;
  if (dur > 0) {
    this->duration = dur > MAX_DURATION ? MAX_DURATION : dur;
  }

  uint8_t mask = 1 << (2 * (this->id - 1));
  set_stations_status(mask, enable_pin);

  report_status(*this);
}

void Station::stop() {
  DEBUG_PRINT("Stopping station [");
  DEBUG_PRINT(this->id);
  DEBUG_PRINTLN("]");
  this->started = 0;
  this->is_active = false;
  this->duration = 0;

  uint8_t mask = 2 << (2 * (this->id - 1));
  set_stations_status(mask, enable_pin);

  report_status(*this);
}

void Station::to_string(char* s) {
  sprintf(s, "Station[%d] { enable_pin: %d, is_active: %d, started: %lld, duration: %lu, cron: '%s' }", id, enable_pin, is_active, started, duration, cron);
}

void StationController::init(NTPClient *time_client) {
  DEBUG_PRINTLN("Initializing...");

  m_time_client = time_client;

  EEPROM.begin(EEPROM_SIZE);

  setup_sr();

  load();

  print_state();

  m_time_client->update();

  mqttcli::init([this](char *topic, byte *payload, uint32_t length) {
    DEBUG_PRINT("MQTT Message arrived [");
    DEBUG_PRINT(topic);
    DEBUG_PRINT("] ");
    DEBUG_PRINTLN();

    if (starts_with("lawn-irrigation/interface-mode/set", topic))  {
      process_topic_mode_set(payload, length); // retained message
    } else if (starts_with("lawn-irrigation/enabled/set", topic))  {
      process_topic_enabled_set(payload, length); // retained message
    } else if (starts_with("lawn-irrigation/station", topic))  {
      Station *station = get_station_from_topic(topic);
      if (station != NULL) {
        if (m_interface_mode && index_of(topic, "set") > 0) {
          process_topic_station_set(*station, payload, length);
        } else if (m_interface_mode && index_of(topic, "state") > 0) {
          process_topic_station_state(*station);
        } else if (index_of(topic, "config") > 0) {
          process_topic_station_config(*station, payload, length); // retained message
        }
      }
    }
  });

  // subscribe to messages
  mqttcli::subscribe("lawn-irrigation/+/config");
  mqttcli::subscribe("lawn-irrigation/+/set");

  // receive and process retained messages
  int i = 10;
  while (i-- > 0) {
    mqttcli::loop();
    delay(100);
  }

  DEBUG_PRINTLN("MQTT init complete.");
}

void StationController::loop() { mqttcli::loop(); }

void StationController::set_interface_mode(bool mode) {
    m_interface_mode = mode;

    save();

    report_interface_mode_state();
  }

// TOPIC format: lawn-irrigation/station#/set
Station *StationController::get_station_from_topic(const char* topic) {
  uint8_t station_id = get_station_id(topic);
  if (station_id > 0 && station_id <= NUM_STATIONS) {
    return &(m_stations[station_id - 1]);
  }

  return NULL;
}

void StationController::check_stop_stations(bool force) {
  for (int i = 0; i < NUM_STATIONS; i++) {
    Station &station = this->m_stations[i];
    time_t now = m_time_client->getEpochTime();
    if (station.is_active == true) {
      if (force || ((now - station.started) > station.duration)) {
        station.stop();
      }
    }
  }
}

StationEvent StationController::next_station_event() {
  StationEvent next_event;

  for (int i = 0; i < NUM_STATIONS; i++) {
    Station &station = m_stations[i];
    if (station.is_active == true) {
      time_t t = station.started + station.duration;
      if (next_event.time == 0 || next_event.time > t) {
        next_event.id = station.id;
        next_event.time = t;
        next_event.type = EventType::STOP;
      }
    } else {
      if (strlen(station.cron) > 0) {
        time_t t = get_next_station_start(station.cron, m_time_client->getEpochTime());
        if (next_event.time == 0 || next_event.time > t) {
          next_event.id = station.id;
          next_event.time = t;
          next_event.type = EventType::START;
        }
      }
    }
  }

  m_station_event = next_event;

  save();

  DEBUG_PRINT("Next Station Event: ");
  char msg[100];
  next_event.to_string(msg);
  DEBUG_PRINTLN(msg);

  return next_event;
}

void StationController::process_station_event() {
  if (!m_interface_mode && m_station_event.id != -1) {
    check_stop_stations();
    time_t now = m_time_client->getEpochTime();
    if (m_station_event.type == START && (m_station_event.time < (now + 20)) && (m_station_event.time > (now - 120))) { // a threshold of 120 seconds to discard old start events
      if (m_enabled) {
        check_stop_stations(true); // if we are starting a station, all other stations must be stopped
        Station &st = m_stations[m_station_event.id - 1];
        st.start(now);

        save();
      } else {
        DEBUG_PRINTLN("Skipping station START event since the system is disabled.");
      }
    } else if (m_station_event.type == START) {
      char msg[200];
      sprintf(msg, "Scheduled START event out of sync with the system time...\nScheduled: '%lld' vs Now: '%lld' \nSkipping event!", m_station_event.time, now);

      DEBUG_PRINTLN(msg);

      mqttcli::publish("lawn-irrigation/log", msg, true);
    }
  }
}

void StationController::process_topic_enabled_set(byte *payload, uint32_t length) {
  DEBUG_PRINTLN("Processing topic 'lawn-irrigation/enabled/set'...");

  char payload_str[length + 1];
  memcpy(payload_str, payload, length);
  payload_str[length] = '\0';

  m_enabled = strcmp(payload_str, "on") == 0;

  DEBUG_PRINTLN("Topic 'lawn-irrigation/enabled/set' done.");
}

void StationController::process_topic_station_set(Station &station, byte *payload, uint32_t length) {
  DEBUG_PRINTLN("Processing topic 'lawn-irrigation/station/set'...");

  // get op and duration
  char payload_str[length + 1];
  memcpy(payload_str, payload, length);
  payload_str[length] = '\0';

  if (starts_with("on", payload_str)) {
    check_stop_stations(true); // if we are starting a station, all other stations must be stopped
    char dur[20];
    substr(payload_str, dur, index_of(payload_str, "|") + 1);
    
    station.start(m_time_client->getEpochTime(), atoi(dur));
  } else if (starts_with("off", payload_str)) {
    station.stop();
  }

  save();

  DEBUG_PRINTLN("Topic 'lawn-irrigation/station/set' done.");
}

void StationController::process_topic_station_state(Station &station) {
  DEBUG_PRINTLN("Processing topic 'lawn-irrigation/station/state'...");

  report_status(station);

  DEBUG_PRINTLN("Topic 'lawn-irrigation/station/state' done.");
}

void StationController::process_topic_mode_set(byte *payload, uint16_t length) {
  DEBUG_PRINTLN("Processing topic 'lawn-irrigation/interface-mode/set'...");
  
  // get mode
  char payload_str[length + 1];
  memcpy(payload_str, payload, length);
  payload_str[length] = '\0';

  m_interface_mode = strncmp("on", payload_str, 2) == 0;

  save();

  report_interface_mode_state();

  DEBUG_PRINTLN("Topic 'lawn-irrigation/interface-mode/set' done.");
}

void StationController::process_topic_station_config(Station &station, byte *payload, uint32_t length) {
  DEBUG_PRINTLN("Processing topic 'lawn-irrigation/station/config'...");

  // get mode
  char payload_str[length + 1];
  memcpy(payload_str, payload, length);
  payload_str[length] = '\0';

  int sep_index = index_of(payload_str, "|");

  substr(payload_str, station.cron, 0, sep_index);
  char dur[10];
  substr(payload_str, dur, sep_index + 1);

  station.duration = atoi(dur);

  save();

  DEBUG_PRINTLN("Topic 'lawn-irrigation/station/config' done.");
}

void StationController::report_interface_mode_state() {
  mqttcli::publish("lawn-irrigation/interface-mode/state", m_interface_mode ? "on" : "off");
}

void StationController::load() {
  int addr = 0;

  if (EEPROM.read(addr) == EEPROM_MARKER) {
    DEBUG_PRINT("Loading state from EEPROM...");

    addr += 1;

    EEPROM.get(addr, m_station_event);
    addr += sizeof(m_station_event);

    for (int i = 0; i < 3; i++) {
      EEPROM.get(addr, m_stations[i]);
      addr += sizeof(Station);
    }

    DEBUG_PRINTLN("done.");
  }
}

void StationController::save() {
  DEBUG_PRINT("Saving state to EEPROM...");
  int addr = 0;
  EEPROM.write(addr, EEPROM_MARKER);
  addr += 1;

  EEPROM.put(addr, m_station_event);
  addr += sizeof(m_station_event);

  for (int i = 0; i < 3; i++) {
    EEPROM.put(addr, m_stations[i]);
    addr += sizeof(Station);
  }

  EEPROM.commit();

  DEBUG_PRINTLN("done.");
}

void StationController::print_state() {
  DEBUG_PRINTLN("");
  DEBUG_PRINTLN("################################");
  DEBUG_PRINT("System is ");
  DEBUG_PRINTLN(m_enabled ? "ENABLED": "DISABLED");
  DEBUG_PRINT("Interface mode = ");
  DEBUG_PRINTLN(m_interface_mode ? "ON": "OFF");
  DEBUG_PRINTLN("");
  char msg[200];
  m_station_event.to_string(msg);
  DEBUG_PRINTLN(msg);
  DEBUG_PRINTLN("");
  for (int i=0; i < NUM_STATIONS; i++) {
    char s[100];
    m_stations[i].to_string(s);
    DEBUG_PRINTLN(s);
  }
  DEBUG_PRINTLN("################################");
}

static void set_sr(uint8_t value) {
  shiftOut(SR_SERIAL_INPUT, SR_CLK, LSBFIRST, value);

  digitalWrite(SR_STORAGE_CLK, LOW);
  digitalWrite(SR_STORAGE_CLK, HIGH);
  digitalWrite(SR_STORAGE_CLK, LOW);
}

static void set_stations_status(uint8_t status, uint8_t enable_pin) {
  set_sr(status);

  digitalWrite(SR_OUTPUT_ENABLED, LOW);
  delay(50);
  digitalWrite(enable_pin, HIGH);
  delay(1000);
  digitalWrite(enable_pin, LOW);
  digitalWrite(SR_OUTPUT_ENABLED, HIGH);
}

static void setup_sr() {
  digitalWrite(STATION_1_EN_PIN, LOW);
  pinMode(STATION_1_EN_PIN, OUTPUT);
  digitalWrite(STATION_2_EN_PIN, LOW);
  pinMode(STATION_2_EN_PIN, OUTPUT);
  digitalWrite(STATION_3_EN_PIN, LOW);
  pinMode(STATION_3_EN_PIN, OUTPUT);

  digitalWrite(SR_OUTPUT_ENABLED, HIGH);
  pinMode(SR_OUTPUT_ENABLED, OUTPUT);

  pinMode(SR_SERIAL_INPUT, OUTPUT);

  digitalWrite(SR_CLK, LOW);
  pinMode(SR_CLK, OUTPUT);

  digitalWrite(SR_STORAGE_CLK, LOW);
  pinMode(SR_STORAGE_CLK, OUTPUT);
}

static time_t get_next_station_start(const char *cron, time_t date) {
  cron_expr ce;
  const char *err = NULL;
  cron_parse_expr(cron, &ce, &err);

  if (err != NULL) {
    DEBUG_PRINT("Error while parsing the CRON expr - ");
    DEBUG_PRINTLN(err);
  }

  return cron_next(&ce, date);
}

static void report_status(const Station &station) {
  char buf[64];
  sprintf(buf, "lawn-irrigation/station%d/state", station.id);
  mqttcli::publish(buf, station.is_active ? "on" : "off");
}

// assuming that the station ID is a single digit
static uint8_t get_station_id(const char *topic) {
  char *found = strstr(topic, "/station");
  char *pos = found ? found + 8 : NULL;
  if (pos != NULL) {
    return (*pos) - '0';
  }

  return 0;
}

static int index_of(const char *str, const char *findstr) {
  char *pos = strstr(str, findstr);
  return pos ? pos - str : -1;
}

static bool starts_with(const char* start_str, const char* str) {
  return strncmp(start_str, str, strlen(start_str)) == 0;
}

static void substr(char s[], char sub[], int p, int l) {
   int c = 0;
   
   while (c < l) {
      sub[c] = s[p + c];
      c++;
   }
   sub[c] = '\0';
}

static void substr(char s[], char sub[], int p) {
   int c = 0;
   
   char ch;

   do {
    ch = s[p + c];
    sub[c] = ch;
    c++;
   } while (ch != '\0');

   sub[c] = '\0';
}

} // namespace sprinkler_controller
