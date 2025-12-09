#include "Adafruit_DS248x.h"
#include <Wire.h>

// SETTINGS
#define DEBUG_MODE 0 

#define DS2484_SLEEP_PIN PA7
#define DS248X_ADDRESS 0x18

// RAK3172 Standard I2C Pins 
#define PIN_WIRE_SDA WB_I2C1_SDA
#define PIN_WIRE_SCL WB_I2C1_SCL

// DS18B20 Commands
#define DS18B20_CMD_SKIP_ROM 0xCC
#define DS18B20_CMD_CONVERT_T 0x44
#define DS18B20_CMD_READ_SCRATCHPAD 0xBE

#if DEBUG_MODE
  #define DEBUG_PRINTLN(x) Serial.println(x)
  #define DEBUG_PRINTF(x, ...) Serial.printf(x, __VA_ARGS__)
#else
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(x, ...)
#endif

Adafruit_DS248x ds248x;
uint32_t transmit_interval = 15000; 
float lastValidTemp = 25.0;

// LoRaWAN Creds
uint8_t app_eui[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
uint8_t device_eui[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
uint8_t app_key[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

uint8_t crc8(uint8_t *data, uint8_t len) {
  uint8_t crc = 0;
  for (uint8_t i = 0; i < len; i++) {
    uint8_t inbyte = data[i];
    for (uint8_t j = 0; j < 8; j++) {
      uint8_t mix = (crc ^ inbyte) & 0x01;
      crc >>= 1;
      if (mix) crc ^= 0x8C;
      inbyte >>= 1;
    }
  }
  return crc;
}

float readTemperature() {
  float temp = -127.0;

  digitalWrite(DS2484_SLEEP_PIN, HIGH);
  delay(5); 

  Wire.begin();
  Wire.setClock(400000);

  if (ds248x.begin(&Wire, DS248X_ADDRESS)) {
    
    // --- Perform Conversion ---
    ds248x.OneWireReset();
    ds248x.OneWireWriteByte(DS18B20_CMD_SKIP_ROM); 
    ds248x.OneWireWriteByte(DS18B20_CMD_CONVERT_T); 
    delay(750);

    // --- Read Data ---
    ds248x.OneWireReset();
    ds248x.OneWireWriteByte(DS18B20_CMD_SKIP_ROM); 
    ds248x.OneWireWriteByte(DS18B20_CMD_READ_SCRATCHPAD); 
    
    uint8_t data[9];
    bool success = true;
    for (int i = 0; i < 9; i++) {
      if (!ds248x.OneWireReadByte(&data[i])) { success = false; break; }
    }

    if (success && crc8(data, 8) == data[8]) {
      int16_t raw = (data[1] << 8) | data[0];
      temp = (float)raw / 16.0;
    }
  }

  Wire.end(); 
  pinMode(PIN_WIRE_SDA, INPUT);
  pinMode(PIN_WIRE_SCL, INPUT);
  digitalWrite(DS2484_SLEEP_PIN, LOW); 

  return temp;
}

void sensor_handler(void *) {
  if (api.lorawan.njs.get() == 0) return;

  float temperature = readTemperature();

  if (temperature <= -100.0) temperature = lastValidTemp;
  else if (temperature == 0.0 || temperature < -40.0 || temperature > 125.0) temperature = lastValidTemp;
  else lastValidTemp = temperature;
  
  DEBUG_PRINTF("T: %.2f\n", temperature);

  int16_t tempScaled = (int16_t)(temperature * 100);
  uint8_t payload[2] = { (uint8_t)(tempScaled >> 8), (uint8_t)(tempScaled & 0xFF) };
  
  api.lorawan.send(sizeof(payload), payload, 1, true, 1);
}

void receiveCallback(SERVICE_LORA_RECEIVE_T *data) {
  if (data->Port == 1 && data->BufferSize == 2) {
    uint16_t new_interval = (data->Buffer[0] << 8) | data->Buffer[1];
    if (new_interval >= 10 && new_interval <= 3600) {
      transmit_interval = (uint32_t)new_interval * 1000;
      api.system.timer.stop(RAK_TIMER_0);
      api.system.timer.start(RAK_TIMER_0, transmit_interval, NULL);
    }
  }
}

void setup() {
  #if DEBUG_MODE
    Serial.begin(115200);
    delay(2000);
  #else
    Serial.end(); 
  #endif

  pinMode(DS2484_SLEEP_PIN, OUTPUT);
  digitalWrite(DS2484_SLEEP_PIN, LOW); // Start powered down

  // SETUP LORAWAN
  api.lorawan.nwm.set();
  api.lorawan.njm.set(1);
  api.lorawan.deviceClass.set(0);
  api.lorawan.band.set(4);
  api.lorawan.deui.set(device_eui, 8);
  api.lorawan.appeui.set(app_eui, 8);
  api.lorawan.appkey.set(app_key, 16);
  api.lorawan.registerRecvCallback(receiveCallback);
  
  api.lorawan.join();

  api.system.timer.create(RAK_TIMER_0, sensor_handler, RAK_TIMER_PERIODIC);
  api.system.timer.start(RAK_TIMER_0, transmit_interval, NULL);
}

void loop() {
  api.system.scheduler.task.destroy();
}
