#pragma once
#ifndef _SETTINGS_H_
#define _SETTINGS_H_

#include <Arduino.h>
#include <EEPROM.h>

#define EEPROM_SETTINGS_SIZE 1024

#define DEVICE_NAME "Sprinkler"
#define STRING_LENGTH 40

class Settings
{
  // change eeprom config version ONLY when new parameter is added and need reset the parameter
  unsigned int configVersion = 1;

public:
  struct Data
  {                                   // do not re-sort this struct
    unsigned int coVers;              // config version, if changed, previus config will erased
    char deviceName[STRING_LENGTH];   // device name
    char mqttServer[STRING_LENGTH];   // mqtt Server adress
    char mqttUser[STRING_LENGTH];     // mqtt Username
    char mqttPassword[STRING_LENGTH]; // mqtt Password
    char mqttTopic[STRING_LENGTH];    // mqtt publish topic

  } data;

  void load()
  {
    memset(&data, 0, sizeof(data));
    EEPROM.begin(EEPROM_SETTINGS_SIZE);
    EEPROM.get(0, data);
    EEPROM.end();
    bool ret = coVersCheck();
    sanitycheck();
    if (ret)
    {
      save();
    }
  }

  void save()
  {
    sanitycheck();
    EEPROM.begin(EEPROM_SETTINGS_SIZE);
    EEPROM.put(0, data);
    EEPROM.commit();
    EEPROM.end();
  }

  void reset()
  {
    data = {};
    save();
  }

private:
  void sanitycheck()
  {
    validateString(data.deviceName, DEVICE_NAME);
    validateString(data.mqttServer, "");
    validateString(data.mqttUser, "");
    validateString(data.mqttPassword, "");
    validateString(data.mqttTopic, DEVICE_NAME);
  }

  void validateString(char *field, const char *defaultValue)
  {
    if (strlen(field) == 0 || strlen(field) >= STRING_LENGTH)
    {
      strncpy(field, defaultValue, STRING_LENGTH - 1);
      field[STRING_LENGTH - 1] = '\0';
    }
  }

  bool coVersCheck()
  {
    if (data.coVers != configVersion)
    {
      memset(&data, 0, sizeof(data));
      data.coVers = configVersion;
      return true;
    }
    return false;
  }
};

#endif