current-measurement-cape-inaxxx evaluation

Add support for INA evaluation cape to TI Processor SDK 7.03 on BeagleBone running Kernel 5.4.106

This package adds support for the Hunter Embedded Current Measurement INAXXX evaluation Cape to the BeagleBone (Black and Green supported).
This cape provides the following options for measuring current

INA219 with a 3.3ohm 1% shunt to measure currents in range +/- 97mA
INA226 with a 0.33ohm 1% shunt to measure currents in range +/- 970mA
INA180A4 with a 0.15ohm 1% shunt + ADS1018 ADC to measure currents in range 0-970mA
INA180A4 with a 1.5ohm 1% shunt + ADS1018 ADC to measure currents in range 0-97mA

All shunts are Panasonic ERJ14s



At the linux level it adds:
    an IIO driver for ADS1018.
    enables built in INA219 and INA226 drivers including a fix for a regression introduced in K5.4

At the application level it provides:
    an application to capture data from IIO and store it in an SQL database
    webserver to allow start and stop of capture and downloading of sql file to host
    host side python3 scripts to access SQL files on BBB and use plotly to display the data. The python3 script identifies the BBB from it's avahi packets.

To install:
    install TI Processor SDK
    clone this repository from SDK root directory 
    
    ti-sdk-7-03$ git clone https://github.com/HunterEmbedded/current-measurement-cape-inaxxx

To build: 
    
    ti-sdk-7-03$ cd current-measurement-cape-inaxxx 
    current-measurement-cape-inaxxx$ ./build-ina-measurement.sh

The output of the build process is a pair of tar files

tar-current-measurement-filesystem-bbb/boot_partition.tar.gz tar-current-measurement-filesystem-bbb/rootfs_partition.tar.gz

To create the SD card: Use the TI SDK application bin/create-sdcard.sh and choose the custom file option to programme these boot_partition.tar.gz and rootfs_partition.tar.gz files.


To run:
    There are two examples to help understand the application
    /opt/iio-command-line.sh controls the IIO via sysfs
    /opt/iio-app-test.sh calls the C app to capture using IIO and store to a local database 

    
    The 

