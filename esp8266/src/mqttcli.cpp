#include "mqttcli.h"
#include "log.h"
#include "Settings.h"
#include "stations.h"

#include <PubSubClient.h>

namespace sprinkler_controller::mqttcli {

static WiFiClient espClient;
static PubSubClient mqtt_client(espClient);
static StationController* station_controller_ptr = nullptr;  // Static pointer to StationController

static void mqtt_connect();

void init( MQTT_CALLBACK_SIGNATURE, StationController *station_controller){
  station_controller_ptr = station_controller;

  mqtt_client.setServer(station_controller_ptr->m_settings->data.mqttServer, 1883);
  mqtt_client.setCallback(callback);
  mqtt_connect();
}

void loop() {
  if (!mqtt_client.connected()) {
      mqtt_connect();
    }
    mqtt_client.loop();
}

void publish(const char* topic, const char* payload, bool retained) {
  mqtt_client.publish(topic, payload, retained);
  mqtt_client.flush();
}

void disconnect() {
  mqtt_client.disconnect();
}

void subscribe(const char* topic) {
  mqtt_client.subscribe(topic);
}

static void mqtt_connect() {
  // Loop until we're connected. Max retries: 5
  uint8_t retries = 0;
  while (!mqtt_client.connected() && retries < 5) {
    retries++;
    debug_printf("Attempting MQTT connection...\n");
    // Create a random client ID
    char client_id[20];
    sprintf(client_id, "ESP8266Client-%04ld",random(0xffff));
    // Attempt to connect
    if (mqtt_client.connect(client_id, station_controller_ptr->m_settings->data.mqttUser, station_controller_ptr->m_settings->data.mqttPassword)) {
      debug_printf("connected\n");
    } else {
      debug_printf("failed, rc=%d try again in 5 seconds", mqtt_client.state());

      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

} // namespace sprinkler_controller