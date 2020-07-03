smopiVTI - cheap and easy to build Arduino Video Time Inserter
--------------------------------------------------------------

This is a video time inserter that inserts GPS time information into a video stream. 

I translated the comments and screen messages to English and adapted it to a cheap and small "MicroOSD osd v2.3.5" AB7456 + Atmega328 video inserter board intended for drone use from China. 
This required some pin changes and the use of a pin change interrupt instead of a normal interrupt for the PPS signal as only analog pins are broken out. Also the font in the 7456 chip needs to be replaced. 
To use the original hardware unset the MOSD24 flag. As i do not have this hardware i did not test this. 

The GPS receiver i use is a GNSS GG-1802 module, also from China. 

Aart, 04-2019

Update 06-2020

The MicroOSD osd v2.3.5 board did not function reliable nor easy to get after the first one, so for a next inserter i used the OSD shield like the original project. 
The GPS receiver is on a few meters of cable. A terminator resistor on the beginning of the line (UTP, 110 ohm) was nessesary to avoid ringing and double interrupts. 

Original README: 

URL: http://smopi.news.nstrefa.pl/index.php?pages/Video-Time-Inserter

Wymagania sprzętowe:
- Arduino UNO,
- odbiornik GPS U-Blox NEO-6M lub zgodny,
- VideoOverlayShield MAX7456.

VTI umożliwia "znakowanie" czasem poszczególnych klatek w analogowym sygnale wideo PAL lub NTSC.

Autor: Piotr Smolarz, smopi.pl@gmail.com
