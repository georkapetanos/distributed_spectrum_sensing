## Software system for relaying statistics from specturm monitoring with an MQTT connection

### Software

- ciio: industrial i/o interface, fourier transform using fftw3 and signal processing, plotting using gnuplot, relaying to mosquitto server

### Requirements

- libfftw3-dev
- libmosquitto-dev
- libiio-dev
- libsqlite3-dev
- gnuplot

### Usage

* Relay information to MQTT server, center frequency 100.2 MHz, 5 Msamples/s
* ./ciio --freq 1002e5 --rate 5e6 --uri ip:172.16.1.2 --sense

* Create a plot using fft bins
* ./ciio --freq 1002e5 --rate 5e6 --uri ip:172.16.1.2 --plot
