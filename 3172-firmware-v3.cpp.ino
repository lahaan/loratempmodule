#include <OneWire.h>

// Debug mode - 0 for "prod" / 1 for debug ON 

/*
ver3, ULP not working still (sub 2uA powerdraw @ sleep), cd: 10/19/25
libraries:
Just OneWire (microDS18B20/DallasTemp don't work); standard RUI3 setup
*/


#define DEBUG_MODE 0

#if DEBUG_MODE
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINT_HEX(x) Serial.print(x, HEX)
  #define DEBUG_PRINTLN(x) Serial.println(x)
  #define DEBUG_PRINTF(x, ...) Serial.printf(x, __VA_ARGS__)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINT_HEX(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(x, ...)
#endif

#define ONE_WIRE_BUS PA7
#define DS18B20_CONVERT_TEMP 0x44
#define DS18B20_READ_SCRATCHPAD 0xBE
#define DS18B20_WRITE_SCRATCHPAD 0x4E
#define DS18B20_COPY_SCRATCHPAD 0x48
#define DS18B20_RECALL_EEPROM 0xB8

OneWire oneWire(ONE_WIRE_BUS);

uint8_t rom[8];
uint32_t transmit_interval = 15000; // Default 60 seconds (in milliseconds)
float lastValidTemp = 25.0;

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

float readTemperature(uint8_t *rom) {
  oneWire.reset();
  oneWire.select(rom);
  oneWire.write(DS18B20_CONVERT_TEMP);
  delay(800);
  
  oneWire.reset();
  oneWire.select(rom);
  oneWire.write(DS18B20_READ_SCRATCHPAD);
  
  uint8_t data[9];
  for (int i = 0; i < 9; i++) {
    data[i] = oneWire.read();
  }
  
  if (crc8(data, 8) != data[8]) {
    DEBUG_PRINTLN("CRC check failed!");
    return -127.0;
  }
  
  int16_t raw = (data[1] << 8) | data[0];
  if (raw == 0) {
    DEBUG_PRINTLN("RAW scratchpad = 0, rejected")
    return -127.0;
  }
  float temp = (float)raw / 16.0;
  
  if (temp < -30.0 || temp > 110.0) {
    DEBUG_PRINTF("Temperature out of range: %.2f°C\n", temp);
    return -127.0;
  }
  
  return temp;
}

bool setResolution12Bit(uint8_t *rom) {
  oneWire.reset();
  oneWire.select(rom);
  oneWire.write(DS18B20_READ_SCRATCHPAD);
  
  uint8_t data[9];
  for (int i = 0; i < 9; i++) {
    data[i] = oneWire.read();
  }
  
  oneWire.reset();
  oneWire.select(rom);
  oneWire.write(DS18B20_WRITE_SCRATCHPAD);
  oneWire.write(data[2]);
  oneWire.write(data[3]);
  oneWire.write(0x7F);
  
  oneWire.reset();
  oneWire.select(rom);
  oneWire.write(DS18B20_COPY_SCRATCHPAD);
  delay(10);
  
  oneWire.reset();
  oneWire.select(rom);
  oneWire.write(DS18B20_RECALL_EEPROM);
  delay(10);
  
  oneWire.reset();
  oneWire.select(rom);
  oneWire.write(DS18B20_READ_SCRATCHPAD);
  
  uint8_t verify[9];
  for (int i = 0; i < 9; i++) {
    verify[i] = oneWire.read();
  }
  
  return (verify[4] == 0x7F);
}

// Timer callback - this is where the work happens
void sensor_handler(void *) {
  // Check if joined
  if (api.lorawan.njs.get() == 0) {
    DEBUG_PRINTLN("Not joined yet");
    return;
  }
  
  float temperature = readTemperature(rom);
  
  // Handle errors
  if (temperature == -127.0) {
    DEBUG_PRINTLN("Error reading - using last valid");
    temperature = lastValidTemp;
  }
  
  // Reject suspicious readings
  if (temperature == 0.0 || temperature < -30.0 || temperature > 105.0) {
    DEBUG_PRINTF("Suspicious: %.2f°C, using %.2f°C\n", temperature, lastValidTemp);
    temperature = lastValidTemp;
  } else {
    lastValidTemp = temperature;
  }
  
  DEBUG_PRINTF("Temperature: %.2f°C\n", temperature);
  
  // Scale and prepare payload
  int16_t tempScaled = (int16_t)(temperature * 100);
  uint8_t payload[2] = {
    (uint8_t)(tempScaled >> 8),
    (uint8_t)(tempScaled & 0xFF)
  };
  
  // Send packet
  if (api.lorawan.send(sizeof(payload), payload, 1, true, 1)) {
    DEBUG_PRINTLN("Packet sent");
  } else {
    DEBUG_PRINTLN("Send failed");
  }
}

void receiveCallback(SERVICE_LORA_RECEIVE_T *data) {
  if (data->BufferSize > 0) {
    DEBUG_PRINT("Downlink on port ");
    DEBUG_PRINT(data->Port);
    DEBUG_PRINT(": ");
    
    for (int i = 0; i < data->BufferSize; i++) {
      DEBUG_PRINTF("%02X ", data->Buffer[i]);
    }
    DEBUG_PRINTLN();
    
    // Port 1: Update interval
    if (data->Port == 1 && data->BufferSize == 2) {
      uint16_t new_interval = (data->Buffer[0] << 8) | data->Buffer[1];
      if (new_interval >= 10 && new_interval <= 3600) {
        transmit_interval = (uint32_t)new_interval * 1000;
        DEBUG_PRINTF("Interval updated to %u seconds\n", new_interval);
        
        // Restart timer
        api.system.timer.stop(RAK_TIMER_0);
        api.system.timer.start(RAK_TIMER_0, transmit_interval, NULL);
      }
    }
  }
}

void setup() {
#if DEBUG_MODE
  Serial.begin(115200);
  delay(2000);
  DEBUG_PRINTLN("DS18B20 LoRaWAN Sensor Starting...");
#endif

  // Find sensor
  int attempts = 0;
  while (!oneWire.search(rom) && attempts < 10) {
    DEBUG_PRINTLN("No DS18B20 found!");
    oneWire.reset_search();
    delay(500);
    attempts++;
  }
  
  if (attempts >= 10) {
    DEBUG_PRINTLN("Sensor not found - halting");
    while(1) delay(1000);
  }

  DEBUG_PRINT("Found ROM: ");
  for (int i = 0; i < 8; i++) {
    if (rom[i] < 16) DEBUG_PRINT("0");
    DEBUG_PRINT_HEX(rom[i]);
    DEBUG_PRINT(" ");
  }
  DEBUG_PRINTLN();

  if (setResolution12Bit(rom)) {
    DEBUG_PRINTLN("12-bit resolution set");
  }

  // Configure LoRaWAN
  api.lorawan.nwm.set();
  api.lorawan.njm.set(1);
  api.lorawan.deviceClass.set(0);
  api.lorawan.band.set(4);
  api.lorawan.deui.set(device_eui, 8);
  api.lorawan.appeui.set(app_eui, 8);
  api.lorawan.appkey.set(app_key, 16);
  
  DEBUG_PRINTLN("LoRaWAN configured");

  // Register callback
  api.lorawan.registerRecvCallback(receiveCallback);

  // Join network
  DEBUG_PRINTLN("Joining network...");
  api.lorawan.join();
  
  while (api.lorawan.njs.get() == 0) {
    DEBUG_PRINTLN("Waiting for join...");
    delay(5000);
  }
  
  DEBUG_PRINTLN("Joined!");

  // Create periodic timer
  if (!api.system.timer.create(RAK_TIMER_0, sensor_handler, RAK_TIMER_PERIODIC)) {
    DEBUG_PRINTLN("Timer create failed!");
  }
  if (!api.system.timer.start(RAK_TIMER_0, transmit_interval, NULL)) {
    DEBUG_PRINTLN("Timer start failed!");
  }
  
  DEBUG_PRINTF("Timer started: %u seconds\n\n", transmit_interval / 1000);
  
  // Send first packet immediately
  sensor_handler(NULL);
}

void loop() {
  // System will sleep between timer events
  api.system.scheduler.task.destroy();
}
