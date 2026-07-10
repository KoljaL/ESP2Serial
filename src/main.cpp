#include <Arduino.h>

#define BAUD_RATE 115200

// Pins für das serielle Zielgerät (Standard-Pins für Serial2 beim V4)
#define RX2_PIN 16
#define TX2_PIN 17

void setup()
{
  // UART0: Verbindung zum PC über USB
  Serial.begin(BAUD_RATE);

  // UART2: Verbindung zum externen Gerät
  Serial2.begin(BAUD_RATE, SERIAL_8N1, RX2_PIN, TX2_PIN);
}

void loop()
{
  // Vom PC empfangen -> an Zielgerät senden
  if (Serial.available())
  {
    Serial2.write(Serial.read());
  }

  // Vom Zielgerät empfangen -> an PC senden
  if (Serial2.available())
  {
    Serial.write(Serial2.read());
  }
}
