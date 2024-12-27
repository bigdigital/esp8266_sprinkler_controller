#pragma once
#ifndef _LED_H_
#define _LED_H_


#include <Arduino.h>
/*
  Blinking LED = Relais Off
  Waveing LED = Relais On
  every 5 seconds:
  1x all ok - Working
  2x no Inverter Connection
  3x no MQTT Connection
  4x no WiFi Connection
*/
 
enum BlinkState {
  WAIT,
  BLINK
};

#define LED_PIN D0

unsigned long previousMillis = 0;
unsigned long waitInterval = 5000; // 5 seconds wait
unsigned long blinkDelay = 200; // 200ms delay between fast blinks

int fastBlinkCount = 1; // Start with 1 fast blink
int currentFastBlink = 0;
bool ledState = LOW; // Current LED state
BlinkState blinkState = WAIT;

void led_setup() {
  pinMode(LED_PIN, OUTPUT); // Set LED pin as an output
  digitalWrite(LED_PIN, LOW); // Ensure the LED starts off
}

void led_loop() {
  unsigned long currentMillis = millis();

  switch (blinkState) {
    case WAIT:
      if (currentMillis - previousMillis >= waitInterval) {
        previousMillis = currentMillis;
        blinkState = BLINK;
        currentFastBlink = 0; // Reset fast blink counter
      }
      break;

    case BLINK:
      if (currentFastBlink < fastBlinkCount) {
        if (currentMillis - previousMillis >= blinkDelay) {
          debug_printf("*");
          previousMillis = currentMillis;
          ledState = !ledState; // Toggle LED state
          digitalWrite(LED_PIN, ledState);

          if (!ledState) { // Count completed blinks when LED turns off
            currentFastBlink++;
          }
        }
      } else {
        blinkState = WAIT; // Go back to waiting
        fastBlinkCount = (fastBlinkCount % 3) + 1; // Cycle between 1, 2, and 3 blinks
        previousMillis = currentMillis; // Reset timer
        digitalWrite(LED_PIN, LOW); // Ensure the LED is off
      }
      break;
  }
}


#endif