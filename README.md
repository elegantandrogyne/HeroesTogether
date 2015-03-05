#HeroesTogether
Forked from MewPro, developped for Lentimax: multiple-GoPro array for 3D photography

Arduino BacPac™ for GoPro Hero 3+ Black: GoPro can be controlled by Arduino Pro Mini attached on Herobus.

Resources:
* Introduction to MewPro, the original project: [http://mewpro.cc/?p=226]
* Schematic Drawing of MewPro: [http://mewpro.cc/?p=204]
* List of GoPro Serial Commands: [http://mewpro.cc/2014/10/14/list-of-i%C2%B2c-commands/]
* Herobus Pinout of GoPro Hero 3+ Black: [http://mewpro.cc/?p=207]

###Objective

Make a device that allows synchronous power up, power down and shutter release on as many GoPro cameras as you want! Construct an array of cameras for 3D still or motion capture, then fire them exactly at the same time. The system is modular and scalable.
The BacPacs™ are connected via a parallel bus (2 lines + GND) with any device that can short 3.3V to GND - no matter if it's two buttons, or another Arduino, Raspberry Pi etc. The controller can function as a time lapse device etc. Connecting multiple controllers is also possible.

HeroesTogether is a vastly simplified fork of MewPro. Most functionality has been removed, i.e.
* shutter release without debouncing,
* time alarms,
* IR remote control,
* light sensor control,
* PIR sensor control,
* video motion detect
* serial port communication.

What is left are routines for communicating via Herobus, slave mode setting, camera on, shutter release with a switch (w/ debouncing).
Added a new functionality:  remote power on/off on a switch, and video/photo mode setting with camera reboot.

------

###How To Compile
Use Arduino IDE for compiling and uploading the source to your Arduino Pro Mini 328.

------

###Controlling it!
HeroesTogether uses two lines connected via 1N4148 diodes for separation:
* 3 - power on (switch on) / power off (switch off). Connect on-off SPST switch here.
* 5 - shutter release or movie start-stop (connect momentary NO SPST here),
* 7 - camera mode setting: video (switch on) / photo (switch off). Connect on-off SPST here too.

Basically, you connect all your HeroesTogether units in parallel, and you connect the switches for shorting these lines to GND.
You can also make a controller for synchronized timelapse etc.

Have fun!
