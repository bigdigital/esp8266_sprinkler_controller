#pragma once
#ifndef _WEB_SOCKET_CLI_H_
#define _WEB_SOCKET_CLI_H_

#include <arduino.h> 

#include <ESPAsyncWiFiManager.h>

namespace sprinkler_controller
{
    class StationController; // Forward declaration of StationController
}

namespace sprinkler_controller::web_socket
{
    void init(AsyncWebServer *server, StationController *station_controller);
    void notifyClients();
    void loop();
}  

#endif