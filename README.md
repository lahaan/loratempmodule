# loratempmodule
Firmware for the RAK3272S Wisduo module, using DS18B20 &amp; LoRaWAN

# Getting started 
RAK module that supports RUI3, is based on an STM32 MCU and supports the following function(s) found here:
  https://github.com/beegee-tokyo/RUI3-LowPower-Example

On nRF-series of MCUs, the use of DS18B20 or other Onewire sensors should be easier and more widely supported, for example nRF52832 (without LoRa):
  https://github.com/lahaan/nRF52832-DS18B20-temp

DS18B20 wiring: electrically speaking, is dependant on several factors (e.g, wire length, type of module, breadboard*, temperature range etc). Change relative to your own needs, however, in our case, the best solution (as of 11/19/2025) ended up being a 10k pull-up resistor (3V3-DIGITAL), and a 220uF Aluminium Polymber Low-ESR decoupling capacitor (3V3-GND). Generally, though, you do not need a capacitor, and should be fine with a 4.7k PUR. 

Before customizing the firmware or flashing it onto the module, one needs to install necessary software/API and an IDE for it. In our case, we followed official documentation / recommendations for simplicity sake (via RAKWireless themselves), and ended up using Arduino IDE with RUI3 on top. It is also possible to use Visual Studio Code with something like PlatformIO for more flexibility. 
  https://docs.rakwireless.com/product-categories/wisduo/rak3272s-breakout-board/quickstart/

Flashing is relatively straight-forward with a Serial-UART module (connect: 3.3-3.3/G-G/TX-RX/RX-TX (3272S)). It is a bit more "messy" with an STM32 programmer, however, superior in a development sense (SWD debugging, which is, to but it bluntly, J-Link on a budget - iykyk). 

Make sure to add api/app/dev keys. After flashing and adding correct keys into the code and into one's TTN (Thingsstack network in our case), the device should work according to spec &amp; code. 

Example RPC payload JavaScript snippet to send a downlink. If you need to send manually, refer to hex (00 1E = 30s, for example). 

```js

   var method = msg.method;
var payload;

switch(method) {
    case 'set15s':  payload = 'AA8='; break;  // 15 s (00 0F), AA8= is b64
    case 'set30s':  payload = 'AB4='; break;  // 30 s (00 1E)
    case 'set1min': payload = 'ADw='; break;  // 1 min (00 3C)
    case 'set5min': payload = 'AHg='; break;  // 5 min (01 2C)
    case 'set10min':payload = 'Alg='; break;  // 10 min (02 58)
    case 'set15min':payload = 'A4Q='; break;  // 15 min (03 84)
    default:        payload = 'AB4=';
}
```

Example of TTN uplink payload decoder.

```js

function decodeUplink(input) {
  var bytes = input.bytes;

  if (!bytes || bytes.length < 2) {
    return {
      errors: ["Payload too short"]
    };
  }

  // Signed 16b
  var raw = (bytes[0] << 8) | bytes[1];
  if (raw & 0x8000) {
    raw = raw - 0x10000;
  }

  var temp = raw / 100.0;

  // CRC
  if (temp === -127.0) {
    return {
      data: { temperature: null },
      warnings: ["Sensor error or CRC fail"]
    };
  }

  // Range check
  if (temp < -30 || temp > 110) {
    return {
      data: { temperature: null },
      warnings: ["Out-of-range value: " + temp]
    };
  }

  return {
    data: {
      temperature: temp
    }
  };
}
```


