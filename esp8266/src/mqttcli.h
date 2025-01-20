#pragma once
#ifndef _MQTTCLI_H_
#define _MQTTCLI_H_

#include <arduino.h>
#include <PubSubClient.h>
#include <ESP8266WiFi.h>

namespace sprinkler_controller
{
    class StationController; // Forward declaration of StationController
}

namespace sprinkler_controller::mqttcli
{

    void init(StationController *station_controller);
    void loop();
    void publish(const char *topic, const char *payload, bool retained);
    void subscribe(const char *topic);
    void disconnect();

} // namespace sprinkler_controller::mqttcli

#endif