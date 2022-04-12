# inaxx-bbb-evaluation
Support for INA evaluation cape to TI Processor [SDK 7.03](software-dl.ti.com/processor-sdk-linux/esd/AM335X/latest/exports/ti-processor-sdk-linux-am335x-evm-07.03.00.005-Linux-x86-Install.bin) on BeagleBone Black running Kernel 5.4.106

This package will build an SD card img for a BBB based on the AM335x TI SDK v7.03 with support for the Hunter Embedded Current Measurement INAXXX Evaluation Board (v4.1.3). This board is available on [Amazon](https://www.amazon.co.uk/INAxxx-Current-Measurement-Evaluation-Cape/dp/B09TYTXM68).

This board provides the following options for measuring current:

- INA219 with a 0.33ohm 1% shunt to measure currents in range +/- 970mA
- INA180A2 with a 0.082ohm 1% shunt + ADS1018 ADC with FSR of 4.096V to measure currents in range 0-1024mA
- The footprint for an INA226 is also present but not populated due to component shortage.

All shunts are Panasonic ERJ14s



At the linux level it adds:
- an IIO driver for ADS1018.
- enables built in INA219 driver including a fix for a regression introduced in K5.4

At the application level it provides:
- an application to capture data from IIO and store it in an SQL database. This provides an example of on how to use IIO APIs in C code.
- webserver to allow start and stop of capture and downloading of sql file to host.
- host side python3 scripts to access SQL files on BBB and use plotly to display the data. The python3 script identifies the BBB from its avahi packets.

## To install: 
Install TI Processor SDK and clone this repository from SDK root directory
```
    ti-sdk-7-03$ git clone https://github.com/HunterEmbedded/inaxxx-evaluation-bbb
```
Assumption is that git is installed and configured already.

## To build: 
```
    ti-sdk-7-03$ cd inaxxx-evaluation-bbb 
    inaxxx-evaluation-bbb$ ./build-bbb-image-inaxxx.sh
```

The output of the build process is a pair of tar files:
- `tar-ina-evaluation-filesystem-bbb/boot_partition.tar.gz` 
- `tar-ina-evaluation-filesystem-bbb/rootfs_partition.tar.gz`

## To create the SD card: 
Use the TI SDK application `bin/create-sdcard.sh` and choose the custom file option to programme the files `boot_partition.tar.gz` and `rootfs_partition.tar.gz files`.


## To run:
Log in via SSH (address allocated by your DHCP server) or via a terminal over serial. Use the username "root" and password "root". 
There are two examples to help understand the application
- /opt/iio-command-line-v4.1.3.sh controls the IIO via sysfs
- /opt/iio-app-test-v4.1.3.sh calls the C app to capture using IIO and store to a local database 
    
To visualise the data from the host/development PC (assuming BBB is connected via Ethernet to the network). Run this from the root dirctory of the SDK
```
ti-sdk-7-03$ cd python
ti-sdk-7-03/python$ python3 show-current.py
```    
This will connect to the BBB (identified by Avahi packets), list available current capture database files and then allow one to be selected and displayed.
