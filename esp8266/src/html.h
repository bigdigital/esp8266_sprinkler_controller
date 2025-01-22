#pragma once

#include <Arduino.h>  // PROGMEM

static const char HTML_FOOT[] PROGMEM = R"rawliteral(            <div class="info">
                SprinlkerController v%p_sw_version% By <a href="https://github.com/bigdigital/esp8266_sprinkler_controller"
                    target="_blank">bigdigital</a>
            </div> 
        </div>
    </body>
</html>)rawliteral";
static const char HTML_HEAD[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">

<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>%p_device_name%</title>

    <style>
        body {
            background: #f5f5f5;
            max-width: 1200px;
            margin: 0 auto;
            padding: 10px;

            font-family: 'Lato', sans-serif;
            text-shadow: 0 0 1px rgba(255, 255, 255, 0.004);
            font-size: 100%%;
            font-weight: 400;
        }

        .header{
            background: #6b7381;
            color: #bdc1c8;
            margin-bottom: 2rem;
        }

        @media (min-width: 576px) {
            .header {
                padding: 4rem 2rem;
            }
        }

        .header h1 {
            color: #fff;
            margin: .67em 0;
        }

        .header .lead {
            font-size: 1.25rem;
            font-weight: 300;
        }

        .station {
            margin-bottom: 10px;
            background-color: #fff;
            color: #333;
            padding: 10px;
            border: #ddd solid 1px;
            border-radius: 5px;
        }

        .btn {
            display: inline-block;
            font-weight: 400;
            color: #212529;
            text-align: center;
            vertical-align: middle;
            -webkit-user-select: none;
            -moz-user-select: none;
            -ms-user-select: none;
            user-select: none;
            background-color: transparent;
            border: 1px solid transparent;
            padding: .375rem .75rem;
            font-size: 1rem;
            line-height: 1.5;
            border-radius: .25rem;
            transition: color .15s ease-in-out, background-color .15s ease-in-out, border-color .15s ease-in-out, box-shadow .15s ease-in-out;
        }

        .toggle {
            width: 60px;
            height: 38px;
            color: #007bff;
            border-color: #007bff;
            position: relative;
            overflow: hidden;
        }

        .toggle input[type="checkbox"] {
            display: none;
        }

        .toggle-group {
            position: absolute;
            width: 200%%;
            top: 0;
            bottom: 0;
            left: 0;
            transition: left 0.35s;
        }

        .toggle-group label {
            display: flex;
            justify-content: center;
            align-items: center;
        }

        .toggle-group label,
        .toggle-group span {
            cursor: pointer;
        }

        .toggle.off .toggle-group {
            left: -100%%;
        }

        .toggle-on {
            position: absolute;
            top: 0;
            bottom: 0;
            left: 0;
            right: 50%%;
            margin: 0;
            border: 0;
            border-radius: 0;
        }

        .toggle-off {
            position: absolute;
            top: 0;
            bottom: 0;
            left: 50%%;
            right: 0;
            margin: 0;
            border: 0;
            border-radius: 0;
            box-shadow: none;
        }

        .toggle-handle {
            position: relative;
            margin: 0 auto;
            padding-top: 0px;
            padding-bottom: 0px;
            height: 100%%;
            width: 0px;
            border-width: 0 1px;
            background-color: #fff;
        }

        .toggle .toggle-handle {
            color: #007bff;
            border-color: #007bff;
        }

        .toggle:hover .toggle-handle {

            opacity: 0.5;
        }

        .toggle.btn {
            min-width: 3.7rem;
            min-height: 2.15rem;
        }

        .toggle-on.btn {
            padding-right: 1.5rem;
            color: #fff;
            background-color: #007bff;
            border-color: #007bff;
        }

        .toggle-off.btn {
            padding-left: 1.5rem;
            color: #212529;
            background-color: #f8f9fa;
            border-color: #f8f9fa;
        }

        .toggle-on.btn:hover {
            color: #fff;
            background-color: #0069d9;
            border-color: #0062cc;
        }

        .toggle-off.btn:hover {
            color: #212529;
            background-color: #e2e6ea;
            border-color: #dae0e5;
        }

    </style>
</head>

<body>
    <div class="container">)rawliteral";
static const char HTML_MAIN[] PROGMEM = R"rawliteral(%p_header%


<div class="header">
    <h1>%p_device_name%</h1>
    <p class="lead">Solenoid Statuses</p>
</div>
<div id="status"></div>


<script>
    let socket;
    let reconnectInterval = 1000;
    let maxReconnectInterval = 15000;

    const stationButtons = {};


    window.addEventListener('load', connectWebSocket);


    function connectWebSocket() {
        socket = new WebSocket('ws://' + window.location.hostname + '/ws');
        socket.onopen = function () {
            console.log('WS connected');
            reconnectInterval = 1000;
        };
        socket.onmessage = function (event) {
            const data = JSON.parse(event.data);

            if (data.next_event && data.stations) {
                updateStations(data.stations);
            }
        };
        socket.onerror = function (error) {
            console.error('WS Error:', error);
            socket.close();
        };
        socket.onclose = function () {
            console.log('WS disconnected');
            reconnectWebSocket();
        };
    };

    function reconnectWebSocket() {
        if (reconnectInterval < maxReconnectInterval) {
            reconnectInterval *= 2;
        }
        console.log(`Reconnecting WebSocket in ${reconnectInterval / 1000} seconds...`);

        setTimeout(function () {
            connectWebSocket();
        }, reconnectInterval);
    }


    // Function to create a toggle switch
    function createToggle(station) {
        const toggleWrapper = document.createElement('div');
        toggleWrapper.classList.add('toggle', 'btn');
        toggleWrapper.setAttribute('data-id', station.id);

        const input = document.createElement('input');
        input.type = 'checkbox';
        input.checked = station.is_active === 1;

        const toggleGroup = document.createElement('div');
        toggleGroup.classList.add('toggle-group');

        const labelOn = document.createElement('label');
        labelOn.classList.add('btn', 'toggle-on');
        labelOn.textContent = 'On';

        const labelOff = document.createElement('label');
        labelOff.classList.add('btn', 'toggle-off');
        labelOff.textContent = 'Off';

        const handle = document.createElement('span');
        handle.classList.add('toggle-handle', 'btn', 'btn-light');

        toggleGroup.appendChild(labelOn);
        toggleGroup.appendChild(labelOff);
        toggleGroup.appendChild(handle);

        toggleWrapper.appendChild(input);
        toggleWrapper.appendChild(toggleGroup);

        toggleWrapper.addEventListener('click', () => {
            const isActive = !input.checked;
            input.checked = isActive;
            toggleWrapper.classList.toggle('off', !isActive);

            toggleStationState(station.id, isActive);
        });

        return toggleWrapper;
    }
 
    function toggleStationState(stationId, state) {  
        const currentState = state ? 1 : 0;
        const message = {
            type: "toggle_station",
            id: stationId,
            is_active: currentState
        };
        socket.send(JSON.stringify(message));  
    }

    function updateStations(stations) {
        const container = document.getElementById('status');
        container.innerHTML = '';

        stations.forEach(station => {
            let stationDiv = document.createElement('div');
            stationDiv.classList.add('station');

            const stationInfo = document.createElement('p');

            if (station.is_active){ 
                stationInfo.textContent = `Station ${station.id}, Cron: ${station.cron}, Started: ${getTimeAgo(station.started)}`;
            }
            else{
                stationInfo.textContent = `Station ${station.id}, Cron: ${station.cron}, Stopped`;
            }

            stationDiv.appendChild(stationInfo);

            var button;
            if (stationButtons[station.id]) {
                button = stationButtons[station.id].el;
            } else {
                button = createToggle(station);
                stationButtons[station.id] = { el: button, isActive: station.is_active };
            }
            button.querySelector('input').checked = station.is_active
            button.classList.toggle('off', station.is_active === 0);
            stationDiv.appendChild(button);
            container.appendChild(stationDiv);
        });
    }

    function getTime(unixTimestamp) {
        return new Date(unixTimestamp * 1000).toLocaleString()
    }

    function getTimeAgo(unixTimestamp) {
        const startedTime = unixTimestamp * 1000;
        const currentTime = Date.now();
        const timeElapsed = currentTime - startedTime;

        if (timeElapsed < 60000) {
            // Less than 1 minute
            return `${Math.floor(timeElapsed / 1000)} seconds ago`;
        } else if (timeElapsed < 3600000) {
            // Less than 1 hour
            return `${Math.floor(timeElapsed / 60000)} minutes ago`;
        } else if (timeElapsed < 86400000) {
            // Less than 1 day
            return `${Math.floor(timeElapsed / 3600000)} hours ago`;
        } else {
            // More than 1 day
            return `${Math.floor(timeElapsed / 86400000)} days ago`;
        }
    }
</script>


%p_footer%

<!-- end -->)rawliteral";
