#pragma once
#ifndef _SETTINGS_H_
#define _SETTINGS_H_

#include <Arduino.h>
#include <EEPROM.h>
#include "log.h" 
#include "stations.h"

#define EEPROM_SETTINGS_SIZE sizeof(Data)

#define EEPROM_TOTAL_SIZE (EEPROM_SETTINGS_SIZE + EEPROM_SPRINKLER_SIZE)

#define SETTINGS_START 0
#define SPRINKLER_START (SETTINGS_START + EEPROM_SETTINGS_SIZE)

#define DEVICE_NAME "Sprinkler"
#define STRING_LENGTH 40

class Settings
{
  // change eeprom config version ONLY when new parameter is added and need reset the parameter
  unsigned int configVersion = 3;

public:
  struct Data
  {                                   // do not re-sort this struct
    unsigned int coVers;              // config version, if changed, previus config will erased
    char deviceName[STRING_LENGTH];   // device name
    char mqttServer[STRING_LENGTH];   // mqtt Server adress
    char mqttUser[STRING_LENGTH];     // mqtt Username
    char mqttPassword[STRING_LENGTH]; // mqtt Password
    char ntpServer[STRING_LENGTH];
    unsigned int ntpOffset;
  } data;

  int getSprinklerStart()
  {
      return SPRINKLER_START;
  }

  void load()
  {
    memset(&data, 0, sizeof(data));
    EEPROM.begin(EEPROM_TOTAL_SIZE);
    EEPROM.get(SETTINGS_START, data); 
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
    EEPROM.put(SETTINGS_START, data);
    EEPROM.commit(); 
    writeLog("Save Settings\n");
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
    validateString(data.ntpServer, "ntp1.time.in.ua");
    validateUint(&data.ntpOffset, 0);
  }

  void validateString(char *field, const char *defaultValue)
  {
    if (strlen(field) == 0 || strlen(field) >= STRING_LENGTH)
    {
      strncpy(field, defaultValue, STRING_LENGTH);
      field[STRING_LENGTH - 1] = '\0';
    }
  }

  void validateUint(unsigned int *field, const char defaultValue, unsigned int min_val = 0, unsigned int max_val = 65530)
  {
    if (*field < min_val || *field > max_val)
    {
      *field = defaultValue;
    }
  }

  bool coVersCheck()
  {
    if (data.coVers != configVersion)
    {
      writeLog("Reset Settings to update data\n");
      memset(&data, 0, sizeof(data));
      data.coVers = configVersion;
      return true;
    }
    return false;
  }
};

#endif