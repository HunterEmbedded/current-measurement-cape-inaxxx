#!/bin/bash

# script to build a current measurement system using ADS1018
# Processor SDK7.0.3
# http://software-dl.ti.com/processor-sdk-linux/esd/AM335X/07_03_00_005/exports/ti-processor-sdk-linux-am335x-evm-07.03.00.005-Linux-x86-Install.bin
#
# Tested on Ubuntu 20.04LTS even although TI SDK does not officially support it

# With the git clone we are in directory
# <SDK root>/current-measurement-cape-ads1018. So first step in script is to go back up to SDK root

# set main directories in use

# This is git clone dir with updates
GIT_BASE_DIR=`pwd`
cd ..
# This is the Processor SDK dir
SDK_DIR=`pwd`



################################################################################
# Now manage the build target board and the boot mode (SD card) 
################################################################################

# Base filesystem name
FS_NAME=ina-evaluation


#set environment for build 
export TI_SDK_PATH=${SDK_DIR}

export NATIVE_PATH=x86_64-arago-linux
export TARGET_PATH=armv7ahf-neon-linux-gnueabi
export CROSS_COMPILE="${SDK_DIR}/linux-devkit/sysroots/${NATIVE_PATH}/usr/bin/arm-none-linux-gnueabihf-"
export PATH="$PATH:${SDK_DIR}/linux-devkit/sysroots/${NATIVE_PATH}/usr/bin"
export USR_INCLUDE_PATH="${SDK_DIR}/linux-devkit/sysroots/${TARGET_PATH}/usr"


# Pick up the correct kernel version used in this particular SDK
KERNEL_VER_BASE=linux-5.4.106+gitAUTOINC+023faefa70-g023faefa70
UBOOT=u-boot-2020.01+gitAUTOINC+2781231a33-g2781231a33


export KERNEL_VER=${KERNEL_VER_BASE}-ina


# now patch Rules.mak to handle the new directory name
if [ ! -f Rules.make.orig ]
then
   cp Rules.make Rules.make.orig
fi

sed "s/${KERNEL_VER_BASE}/${KERNEL_VER}/g" Rules.make.orig > Rules.make


# now create board specific file system
FS="${FS_NAME}-filesystem"
TAR_FS="tar-${FS}" 

export INSTALL_PATH="${SDK_DIR}/${FS}"


if [ ! -e ${FS} ]
then
     mkdir ${FS}
else
     cd ${FS}
     sudo rm -r *  
     cd ..
fi
if [ ! -e ${TAR_FS} ]
then
     mkdir ${TAR_FS}
fi


# extract default sdk filesystem to get dropbear (ssh) and lighttpd for this application
cd ${FS}
sudo tar -xJf ../filesystem/tisdk-base-image-am335x-evm.tar.xz .
#sudo tar -xJf ../filesystem/tisdk-default-image-am335x-evm.tar.xz .
cd ..


##### Patch the kernel as required
if [ ! -f kernel.patched ]
then

    cd board-support/
    #if a clean tar file does not exist then create one
    if [ ! -e ${KERNEL_VER_BASE}.tar.gz ]
    then
        tar -czf ${KERNEL_VER_BASE}.tar.gz ${KERNEL_VER_BASE}
        # and move current directory to a backup
        cp -r ${KERNEL_VER_BASE} ${KERNEL_VER_BASE}-clean
        mv ${KERNEL_VER_BASE} ${KERNEL_VER}
    else
        # as tar file exists then delete working build and repopulate with clean tar 
        echo "extract tar kernel"
        sudo rm -r ${KERNEL_VER}
        tar -xf ${KERNEL_VER_BASE}.tar.gz
        mv ${KERNEL_VER_BASE} ${KERNEL_VER}
    fi
	  
    # As 7.02 is missing some files from git commit them here
    cd ${KERNEL_VER}
    git add arch/arm/boot/dts/am335x-boneblack-pru-adc.dts
    git add arch/arm/boot/dts/am335x-pru-adc.dtsi
    git add arch/arm/configs/tisdk_am335x-evm_defconfig
  
    git commit -a -m "add files missing from GIT in SDK"
  
    cd ../..
    # clean it just to be sure
    make linux_clean

    cd board-support/${KERNEL_VER} ||  exit
	  
    git am ${GIT_BASE_DIR}/patches/kernel/0001-enable-IIO-INA2xx-driver.patch || exit
    git am ${GIT_BASE_DIR}/patches/kernel/0002-add-ADS1018-driver-to-IIO-and-connect-it-to-SPI-on-B.patch || exit
    git am ${GIT_BASE_DIR}/patches/kernel/0003-only-do-timespec64_to_ns-on-a-positive-timespec-valu.patch || exit
    git am ${GIT_BASE_DIR}/patches/kernel/0004-add-device-tree-support-for-inaxxx-cape-v4.1.3.patch || exit
    cd ../..
    # create this file to say kernel has been patched. To recreate from scratch delete this file
    touch kernel.patched
fi


##### Make uboot  - actual config is defined in Rules.make
make u-boot


##### Make the kernel 
make linux MAKE_JOBS=8 || exit


#The main linux_install script uses Rules.mak to set filesystem. We are not using that directory here and so
# we will explicitly call the install routines so ${FS} can be passed
#sudo make linux_install
sudo install -d ${FS}/boot
sudo install board-support/${KERNEL_VER}/arch/arm/boot/zImage ${FS}/boot

# reduce filesystem size by removing original zImage
sudo rm ${FS}/boot/zImage-${KERNEL_VER}

sudo install board-support/${KERNEL_VER}/System.map ${FS}/boot
sudo cp -f board-support/${KERNEL_VER}/arch/arm/boot/dts/*.dtb ${FS}/boot/

	
sudo make -C board-support/${KERNEL_VER} ARCH=arm CROSS_COMPILE=${CROSS_COMPILE} INSTALL_MOD_PATH=${SDK_DIR}/${FS} modules_install  

# Build sqlite from source
if [ ! -e sqlite-downloaded ]
then
   mkdir -p sqlite
   # now download sqlite
   cd sqlite
   rm sqlite-amalgamation-3140200.zip
   rm sqlite-autoconf-3140200.tar.gz
   wget https://sqlite.org/2016/sqlite-autoconf-3140200.tar.gz
   tar -xzf sqlite-autoconf-3140200.tar.gz
    
   cd  ..
   touch sqlite-downloaded
fi

if [ ! -e sqlite-built ]
then

   #now configure it 
   cd sqlite/sqlite-autoconf-3140200 || exit
   #CFLAGS = "DSQLITE_DEFAULT_WAL_AUTOCHECKPOINT=100 " ./configure --host=arm-linux-gnueabihf --with-yielding-select=yes RANLIB=${TOOLCHAIN_PATH}/${CROSS_COMPILE}ranlib CC=${TOOLCHAIN_PATH}/${CROSS_COMPILE}gcc AR=${TOOLCHAIN_PATH}/${CROSS_COMPILE}ar LD=${TOOLCHAIN_PATH}/${CROSS_COMPILE}ld
   ./configure --host=arm-none-linux-gnueabihf --with-yielding-select=yes RANLIB=${TOOLCHAIN_PATH}/${CROSS_COMPILE}ranlib CC=${TOOLCHAIN_PATH}/${CROSS_COMPILE}gcc AR=${TOOLCHAIN_PATH}/${CROSS_COMPILE}ar LD=${TOOLCHAIN_PATH}/${CROSS_COMPILE}ld
   # and build it
   make clean
   make 
   cd ../..
   touch sqlite-built
fi

# always install
cd sqlite/sqlite-autoconf-3140200
sudo make install DESTDIR=${INSTALL_PATH}
cd ../..


# Add new path to library search path if it does not already exist
# needs ldconfig to be run once on target
sqliteLibPath=`grep \"/usr/local/lib\" ${INSTALL_PATH}/etc/ld.so.conf`

if [ -z "$sqliteLibPath" ]
then
       sudo sh -c "echo '/usr/local/lib' >> ${INSTALL_PATH}/etc/ld.so.conf"
fi

######################################################################################################
# copy in the app and build it
cp -rp ${GIT_BASE_DIR}/patches/capture-current . || exit

cd capture-current || exit
make CC=${TOOLCHAIN_PATH}/${CROSS_COMPILE}gcc || exit
# create install directory in case it is not already present
sudo mkdir -p ${INSTALL_PATH}/opt
sudo mkdir -p ${INSTALL_PATH}/var/www/sql
sudo make CC=${TOOLCHAIN_PATH}/${CROSS_COMPILE}gcc TARGETDIR=${INSTALL_PATH}/opt install  || exit
sudo mkdir -p ${INSTALL_PATH}/etc/avahi/services  || exit
sudo cp cm.service ${INSTALL_PATH}/etc/avahi/services  || exit
sudo cp check-free-disk.sh ${INSTALL_PATH}/opt/  || exit
sudo mkdir -p ${INSTALL_PATH}/etc/lighttpd

# This overwrites default SDK matrix gui web interface
sudo cp lighttpd-current-capture.conf ${INSTALL_PATH}/etc/lighttpd/lighttpd.conf  || exit

sudo cp test.sh ${INSTALL_PATH}/opt/  || exit
sudo cp iio-command-line-v4-1-3.sh ${INSTALL_PATH}/opt/  || exit
sudo cp iio-app-test-v4-1-3.sh ${INSTALL_PATH}/opt/  || exit


# Auto start the swupdate process using systemctl
#sudo cp cm-systemd.service ${INSTALL_PATH}/lib/systemd/system 
# this is equivalent to "systemctl enable" running on the the target
######################################################################################################
cd ${INSTALL_PATH}
sudo ln -s lib/systemd/system/cm-systemd.service etc/systemd/system/multi-user.target.wants/cm-systemd.service    
cd -


# Now cd to the filesystem directory
cd ${INSTALL_PATH}


# Ensure that eth0 is always used even if SD card used on another board already
sudo sh -c "echo '# BeagleBone: net device()
SUBSYSTEM==\"net\", ACTION==\"add\", DRIVERS==\"?*\", ATTR{dev_id}==\"0x0\", ATTR{type}=\"1\", KERNEL==\"eth*\", NAME=\"eth0\"' > etc/udev/rules.d/70-persistent-net.rules"

# and index page and conf files for run script
sudo mkdir -p var/www
sudo cp -r ${SDK_DIR}/capture-current/www/* var/www/
sudo cp -r ${SDK_DIR}/capture-current/php/* var/www/


# We want lighttpd running on the target. It is in not in base rootfs so we will extract all the files we need from default rootfs
sudo tar --wildcards -xJf ../filesystem/tisdk-default-image-am335x-evm.tar.xz ./usr/sbin/lighttpd ./lib/systemd/system/lighttpd.service ./usr/lib/mod_*.so ./usr/bin/php-cgi
# this is equivalent to "systemctl enable" running on the the target
sudo ln -s lib/systemd/system/lighttpd.service etc/systemd/system/multi-user.target.wants/lighttpd.service    

# We also want dropbear to provide ssh
sudo tar --wildcards -xJf ../filesystem/tisdk-default-image-am335x-evm.tar.xz ./usr/sbin/dropbear* ./etc/rc*.d/*10dropbear ./etc/init.d/dropbear ./etc/pam.d/dropbear ./etc/dropbear ./etc/systemd/system/sockets.target.wants/dropbear.socket ./lib/systemd/system-preset/98-dropbear.preset ./lib/systemd/system/dropbear*.service ./lib/systemd/system/dropbear*.socket


#Copy the python data visualisation script to a clean directory on HOST
if [ ! -e ${SDK_DIR}/python ]
then
   # create directory if required
   mkdir ${SDK_DIR}/python || exit
else
   # clean existing one 
   sudo rm -r ${SDK_DIR}/python/*
fi 

cp ${GIT_BASE_DIR}/patches/python/*.py ${SDK_DIR}/python/ || exit


cd ${INSTALL_PATH}
   
 
# create full tar file for SD filesystem
sudo tar -cJf ../${TAR_FS}/rootfs_partition.tar.xz *
 
# change to boot, and only remove files if that 'cd' command executed correctly 
# remove existing mlo, u-boot from directory, we can't copy symbolic links to FAT  
# ensure it is target filesystem boot and not host /boot !!!
cd ../${FS}/boot && sudo rm -r *

# as we are writing to a FAT ultimately symbolic links don't work, so remove them by writing to target names
sudo cp ../../board-support/${UBOOT}/MLO MLO
sudo cp ../../board-support/${UBOOT}/u-boot.img u-boot.img

sudo tar -cJf ../../${TAR_FS}/boot_partition.tar.xz *



cd ../..
 
echo "FINISHED"
