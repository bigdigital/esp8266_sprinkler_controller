#include "mqttcli.h"
#include "log.h"
#include "Settings.h"
#include "stations.h"

#include <PubSubClient.h>

namespace sprinkler_controller::mqttcli
{

  static WiFiClient espClient;
  static PubSubClient mqtt_client(espClient);
  static StationController *station_controller_ptr = nullptr;
  static char client_id[60];

  static bool mqtt_connect();

  void init(StationController *station_controller)
  {
    station_controller_ptr = station_controller;

    mqtt_client.setServer(station_controller_ptr->m_settings->data.mqttServer, 1883);
        // Forward the callback using a static function or lambda
    mqtt_client.setCallback([](char *topic, uint8_t *payload, unsigned int length) {
        if (station_controller_ptr) {
            station_controller_ptr->mqttcallback(topic, payload, length);
        }
    });

    // Create a random client ID
    sprintf(client_id, "%s-%06X", station_controller_ptr->m_settings->data.deviceName, ESP.getChipId());
  }

  void loop()
  {
    if (!mqtt_client.connected())
    {
      mqtt_connect();
    }
    mqtt_client.loop();
  }

  void publish(const char *topic, const char *payload, bool retained)
  {
    mqtt_client.publish(topic, payload, retained);
    mqtt_client.flush();
  }

  void disconnect()
  {
    mqtt_client.disconnect();
  }

  void subscribe(const char *topic)
  {
    mqtt_client.subscribe(topic);
  }

  static bool mqtt_connect()
  {
    if (strcmp(station_controller_ptr->m_settings->data.mqttServer, "") == 0)
    {
      return false;
    }

    if (!mqtt_client.connected())
    {
      writeLog("Attempting MQTT connection...\n");
      // Attempt to connect
      if (mqtt_client.connect(client_id, station_controller_ptr->m_settings->data.mqttUser, station_controller_ptr->m_settings->data.mqttPassword))
      {
        // subscribe to messages
        subscribe("lawn-irrigation/+/config");
        subscribe("lawn-irrigation/+/set");
        writeLog("Connected\n");
      }
    }
    else
    {
      return false; // Exit if we couldnt connect to MQTT brooker
    }

    return true;
  }

} // namespace sprinkler_controller