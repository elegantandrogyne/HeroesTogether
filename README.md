#gopro-sync
Forked from MewPro, developped for Lentimax: multiple-GoPro array for 3D photography

Arduino BacPac™ for GoPro Hero 3+ Black: GoPro can be controlled by Arduino Pro Mini attached on Herobus.

Resources:
* Introduction to MewPro: [http://mewpro.cc/?p=226]
* Schematic Drawing of MewPro: [http://mewpro.cc/?p=204]
* List of GoPro Serial Commands: [http://mewpro.cc/2014/10/14/list-of-i%C2%B2c-commands/]
* Herobus Pinout of GoPro Hero 3+ Black: [http://mewpro.cc/?p=207]

------

###How To Compile
The following small-factor microcontroller boards are known to work with MewPro at least core functionalities and fit within the GoPro housing. Not all the sensors, however, are supported by each of them.

* Arduino Pro Mini 328 3.3V 8MHz
  - w/ Arduino IDE 1.5.7+
  - if you have troubles on compiling unused or nonexistent libraries, simply comment out #include line as //#include (see Note* below)

* Arduino Pro Micro - 3.3V 8MHz
  - w/ Arduino IDE 1.5.7+
  - if you have troubles in compiling unused or nonexistent libraries, simply comment out #include line as //#include (see Note* below)

------

###Serial Line Commands
By default MewPro is configured to use the serial line for controlling GoPro. All the commands are listed at https://gist.github.com/orangkucing/45dd2046b871828bf592#file-gopro-i2ccommands-md . You can simply type a command string to the serial console followed by a return; for example,

+ `PW0` : shutdown GoPro
+ `TM0E0A0D090F00` : set GoPro clock to 2014-10-13 09:15:00 (hexadecimal of YYYY-MM-DD hh:mm:ss)
+ `SY1` : shutter (camera mode) or start record (video mode)
+ `SY0` : stop record (video mode)

Almost all listed commands that have a label named SET_CAMERA_xxx are usable. Moreover two special command are implemented in MewPro:

+ `@` : GoPro power on
+ `!` : toggle the role of MewPro (slave -> master or master -> slave)

Also the command `!` can be used to write the necessary bytes to onboard blank/new I²C EEPROM chip: In order to work as a fake Dual Hero Bacpac™, MewPro's I²C EEPROM must contain such info.

------

Use one of the following, depending on the type of shutter release:

+ shutter.c : Using external shutters such as CANON Timer Remote Controller TC-80N3.
+ switch.c : Using mechanical switches such as push buttons or reed switches
