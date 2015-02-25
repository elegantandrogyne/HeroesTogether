#gopro-sync
Forked from MewPro, developped for Lentimax: multiple-GoPro array for 3D photography

Arduino BacPac™ for GoPro Hero 3+ Black: GoPro can be controlled by Arduino Pro Mini attached on Herobus.

Resources:
* Introduction to MewPro: [http://mewpro.cc/?p=226]
* Schematic Drawing of MewPro: [http://mewpro.cc/?p=204]
* List of GoPro Serial Commands: [http://mewpro.cc/2014/10/14/list-of-i%C2%B2c-commands/]
* Herobus Pinout of GoPro Hero 3+ Black: [http://mewpro.cc/?p=207]

###Objective

Make a device that allows synchronous power up, power down and shutter release on as many GoPro cameras as you want! Construct an array of cameras for 3D still or motion capture, then fire them exactly at the same time. The system is modular and scalable.
The BacPacs™ are connected via a parallel bus (2 lines + GND) with any device that can short 3.3V to GND - no matter if it's two buttons, or another Arduino, Raspberry Pi etc. The controller can function as a time lapse device etc. Connecting multiple controllers is also possible.

gopro-sync is a vastly simplified fork of MewPro. Most functionality has been removed, i.e.
* shutter release without debouncing,
* time alarms,
* IR remote control,
* light sensor control,
* PIR sensor control,
* video motion detect.

Any routines for controlling the camera via serial port are deprecated as well, and will be removed during further development.

What will be left are routines for communicating via Herobus, master/slave mode setting, camera on, shutter release with a switch (w/ debouncing).
Added a new functionality:  remote power on/off on switch.

------

###How To Compile
Use Arduino IDE for compiling and uploading the source to your Arduino Pro Mini 328.

------

###Controlling it!
goprosync uses two lines connected via 1N4148 diodes for separation:
* 3 - power on/off,
* 5 - shutter release or movie start-stop.

Basically, you connect all your goprosync units in parallel, and you connect two switches for shorting these lines to GND.

Have fun!
