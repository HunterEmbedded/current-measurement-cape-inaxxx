#!/bin/sh

# script to test the foure current capture options on the INA evaluation cape




# remove stop file if it exists
if [ -e /var/www/stopCapture ]
then
    rm /var/www/stopCapture
fi


# INA219
./capture-current-iio --device=ina219 --element=current3 &

# sleep for 10 seconds then stop app by creating a file
sleep 10
touch /var/www/stopCapture
sleep 1
rm /var/www/stopCapture

# INA226
./capture-current-iio --device=ina226 --element=current3 &

# sleep for 10 seconds then stop app by creating a file
sleep 10
touch /var/www/stopCapture
sleep 1
rm /var/www/stopCapture

# INA180 on ADS1018 channel0
./capture-current-iio --device=ads1018 --element=voltage0 --trigger &

# sleep for 10 seconds then stop app by creating a file
sleep 10
touch /var/www/stopCapture
sleep 1
rm /var/www/stopCapture

# INA180 on ADS1018 channel1
./capture-current-iio --device=ads1018 --element=voltage1 --trigger &

# sleep for 10 seconds then stop app by creating a file
sleep 10
touch /var/www/stopCapture
sleep 1
rm /var/www/stopCapture
