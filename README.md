# 39sf040-eeprom-programmer
An EEPROM burner designed for the 39SF0X series chips using the Raspberry Pi Pico. As of 2024, I recently started playing around with the Pi Pico and I loved how versatile and powerful this little cheap board is. There's also a Pi Pico 2, although I can't find any in stock currently as of writing this page. Such is usually the case with the latest tech from the Pi foundation, but give it some time and I'm sure it'll be everywhere. 

# Files included:
- C code for the programmer
- Gerber / Schematic files to create your own
- This readme

# Design:
The circuit is designed to support the 512KB 39SF040 chips by reading file(s) from an SD card and writing them to EEPROM. As of the first iteration of this project, the burner accepts commands via a serial port terminal (default baudrate is 115200). The circuit utilizes a few chips and additional components:
- 3 74HC595 shift registers. Mine are branded "HC595G" and are in the SOIC-16 package. I don't remember where I bought them, either DigiKey or Mouser.
- 2 TXB0108 8-bit bi-directional logic level converters. Mine are digikey part 296-21527-1-ND, they have "YE-08" written on them, and they are in the TSSOP-20 package.
- Adafruit 5V-ready Micro-SD breakout board+. This doesn't need to be 5v compatible, it's entirely driven by 3.3v, it's just what I had on hand. I'm sure if you wanted to, you could just mount a standard microSD card slot onto the PCB and connect the correct pins directly to the Pi Pico as the IO is at 3.3v level. 
- SSD1306 128x64 OLED display - this thing is really cool and very cheap. I got some knockoffs on amazon in a 5-pack for $14.99. It's really optional but I find it much better than watching the serial port to figure out what's going on.
- 4 100nF surface mount caps, I think mine are too small for the part layout on the PCB but it really doesn't matter. Mine came from a book and are labelled "GRM21BR71E104KA01L C0805"
- 1 40-pin DIP ZIF socket - I have like 5 of these leftover from previous iterations of this project, these seem to be cheaper and easier to find than the 32 pin variant we actually need here. If you can find one of those, the bottommost 4 pins on the left and right sides of the socket are unused. If you're using a 40-pin socket like me, just seat the EEPROM at the top of the socket.
- I personally also decided to socket the Pico, as I don't have many of them and the v2 is still out of stock. It's cheap enough to just solder directly, though. I'm also using the Pico H version with the pre-installed headers, so I use 2 rows of 20 square female headers to socket the Pico.

# Quirks, bugs, etc to be improved:
- The nop() function, its usage, and duration could use the most tuning. It's currently set to basially do 500 add instructions in a loop because things don't work quite right unless the delay is roughly that high. I don't recall what the common denominator is (I'm fairly the certain the shift register operations work without much nop time) but it could certainly be fine-tuned and improved to get faster programming time. From the datasheets I have found, I have found the byte program time to be listed as 10 microseconds max, and another version of the datasheet that lists 20 microseconds max. Currently I find that I need to delay_us of 25 to get it to work. What I would recommend is to read the datasheet, follow the table for byte program timing parameters and see if you can more precisely determine how long to delay. It's very interesting to me though, I've been working with this EEPROM for quite a few years now, and as soon as I start working on this iteration of the programmer that Microchip published a new version of this datasheet.
- Currently the filename to read/write to the SD card is hard-coded in the C program. It would be trivial to accept the filename over serial and use that instead. I think I will do that before long.
- There are no mounting holes in the PCB for a case, I would probably add those next time. Currently I am using adhesive-backed rubber feet on the bottom, they fit nicely into the 4 corners of the PCB between the pins of the Pi and the ZIF socket.
