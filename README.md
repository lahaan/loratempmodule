# loratempmodule
Firmware for the RAK3272S Wisduo module, using DS18B20 &amp; LoRaWAN

# Getting started 
RAK module that supports RUI3, is based on STM32 MCUs and supports the following function(s):
  https://github.com/beegee-tokyo/RUI3-LowPower-Example

DS18B20 wiring: electrically speaking, is dependant on the wiring length, type of module, breadboard*, temperature range, and several other factors. Change relative to your own needs, however, in our case, the best solution (as of 11/19/2025) ended up being a 10k pull-up resistor, and 220uF Aluminium Polymber Low-ESR decoupling capacitor (between VDD and GND, of course). Generally, though, you do not need a capacitor, and should be fine with a 4.7k PUR. 

Before flashing firmware onto the module or editing it, one needs to install necessary software / IDE for it. In our case, we followed official documentation / recommendations for simplicity sake (via RAKWireless themselves), and ended up using Arduino IDE with RUI3 on top. It is also possible to use Visual Studio Code with something like PlatformIO. 

Flashing is relatively straight-forward with a Serial-UART module. Hooking it up is obviously 3.3-3.3/G-G/TX-RX/RX-TX (3272S). It is a bit more messy with an STM32 programmer however superior (ability for SWD logs) in development sense (SWD debugging, which is pretty much J-Link on a budget). 

After flashing and adding correct keys into the code and into one's TTN (Thingsstack network was our choice), that has a Gateway connected as well, the device should work normally. 

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


