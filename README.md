# WeatherSense 9000
Home-brew Arduino "WeatherSense" IoT Personal Weather Station receiver, remote, and multiple senders

Hardware
========
Main Receiver - Arduino Uno R3 with Ethernet Shield (wired), nRF24L01+, RTC1307, and 20x4 LCD display
Remote Node - Arduino Uno R3 with Ethernet Shield (wired) and 20x4 LCD display
(3) Sender Nodes - devDuino Sensor Node V4 (ATmega 328) with integrated temperature & humidity sensor
(Sender 1 (ID 0) has BMP085 barometric pressure sensor wired to it)

Main Receiver always on, DC powered.  Receiver Node toggles LCD between 3 sender node readings changing every 30 seconds.
Remote Node always on, DC powered.  Remote Node runs web server waiting for incoming readings from Receiver Node (as web client) via wired Ethernet whenever Receiver gets updates from a sender.
Senders run off one A123 battery each in software low power mode which sleeps senders most of the time.  Senders occasionally wake and send respective readings to Receiver.  Receiver gets RF24 interrupt and accepts readings.  Receiver updates its main display and passes to Remote Node.  Sender battery voltage drop insignificant over a year of use and will most likely die on normal battery shelf life deterioration schedule.

Receiver acts as web server for HTTP web page displaying sender node readings from any browser.  Receiver uploads readings to personal Xively account.

See notes.txt for more hardware info.
