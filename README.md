Web-enabled Arduino Hot Tub controller / monitor / internet bridge

Using X-Board V2.0 (SKU:DFR0162) with Ethernet:WIZ5100 and Arduino UNO Bootloader

http://www.dfrobot.com/wiki/index.php/X-Board_V2_(SKU:DFR0162)

controlling a 3800 watt electric heater through a solid-state relay, and an electric water pump through a different solid state relay (can't share, one's 240v the other is 120v)

reading temperature using a Waterproof DS18B20 Digital temperature sensor

http://www.adafruit.com/products/381

example program for DS18B20 (works perfectly with no changes):
http://bildr.org/2011/07/ds18b20-arduino/

output temperature to an analog temperature gauge (from the exploratorium) using PWM on pin D9

# installing the toolchain

First install the standard avr toolchain:

```
$ sudo apt-get install python-pip gcc-avr avr-libc binutils-avr avrdude
```

# build and flash

Plug in the arduino over usb (and make sure you have permission to write to
/dev/ttyACM0), then do:

```
$ ./make.sh
```
