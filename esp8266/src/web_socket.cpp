#include "web_socket.h"
#include "log.h"
#include "Settings.h"
#include "stations.h"

#include <PubSubClient.h>

namespace sprinkler_controller::web_socket
{
  static AsyncWebSocket ws("/ws");
  static AsyncWebSocketClient *wsClient;

  static StationController *station_controller_ptr = nullptr;

  void handleWebSocketMessage(void *arg, uint8_t *data, size_t len)
  {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
    {
      String message = String((char *)data);
      // Serial.println("WebSocket message received: " + message);

      const char *idKey = "\"id\":";
      const char *stateKey = "\"is_active\":";

      if (message.indexOf("\"type\":\"toggle_station\"") != -1)
      {
        // Extract the id
        int idStart = message.indexOf(idKey) + strlen(idKey);
        int idEnd = message.indexOf(",", idStart);
        uint8_t stationId = message.substring(idStart, idEnd).toInt();

        // Extract is_active
        int stateStart = message.indexOf(stateKey) + strlen(stateKey);
        int stateEnd = message.indexOf("}", stateStart);
        bool isActive = message.substring(stateStart, stateEnd).toInt();

        Station *station = station_controller_ptr->get_station(stationId - 1);

        if (station != NULL)
        {
          if (isActive)
          {
            station_controller_ptr->set_station(*station, START, 0);
          }
          else
          {
            station_controller_ptr->set_station(*station, STOP, 0);
          }
          notifyClients();
        }
      }
    }
  }

  void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
  {
    switch (type)
    {
    case WS_EVT_CONNECT:
      wsClient = client;
      notifyClients();
      break;
    case WS_EVT_DISCONNECT:
      wsClient = nullptr;
      ws.cleanupClients();
      break;
    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len);
      break;
    case WS_EVT_ERROR:
      wsClient = nullptr;
      ws.cleanupClients();
      break;
    default:
      break;
    }
  }

  void init(AsyncWebServer *server, StationController *station_controller)
  {
    station_controller_ptr = station_controller;
    ws.onEvent(onWebSocketEvent);
    server->addHandler(&ws);
  }

  void notifyClients()
  {
    if (wsClient != nullptr && wsClient->canSend())
    {
      char msg[250 * NUM_STATIONS + 200];
      station_controller_ptr->to_json(msg);
      size_t msgLength = strlen(msg);
      AsyncWebSocketMessageBuffer *buffer = ws.makeBuffer(msgLength);
      if (buffer)
      {
        memcpy(buffer->get(), msg, msgLength);
        wsClient->text(buffer);
      }
    }
  }

  void loop()
  {
    ws.cleanupClients();
  }

} // namespace sprinkler_controller