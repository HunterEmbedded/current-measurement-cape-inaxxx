/* Industrialio buffer test code.
 *
 * Copyright (c) 2008 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is primarily intended as an example application.
 * Reads the current buffer setup from sysfs and starts a short capture
 * from the specified device, pretty printing the result after appropriate
 * conversion.
 *
 * Command line parameters
 * generic_buffer -n <device_name> -t <trigger_name>
 * If trigger name is not specified the program assumes you want a dataready
 * trigger associated with the device and goes looking for it.
 *
 */

/* Required for asprintf()  */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/dir.h>
#include <linux/types.h>
#include <string.h>
#include <poll.h>
#include <endian.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <signal.h>
#include "iio_utils.h"
#include "sql-manager.h"


#define SAMPLE_FREQ_HZ 		2400
#define SAMPLE_PERIOD_US	(1000000/SAMPLE_FREQ_HZ)

#define FSR	    		4096			// FSR max 4096mV with 4.096 PGA gain
#define SHUNT    		0.08F
#define INA_GAIN		50.0F                   // INA180A2 with x50 gain

// Calculate the scaling factor that converts from 11bit unsigned ADC (0 to 2048) to uA
#define SCALING_FACTOR 	((FSR*1000.0)/(2048*SHUNT*INA_GAIN))  


t_logData currentSamples;

int find_type_by_name(const char *name, const char *type);





char defaultDevice[]="ads1018";
char *pActualDevice=NULL;
char defaultElement[]="voltage0";
char *pActualElement=NULL;

int actualDeviceChannel;
int actualSampleRate=SAMPLE_FREQ_HZ;
bool actualTrigger=false;

void checkBeforeFree(void * buf)
{
 if (buf)
     free(buf);
 else
     printf("NULL buffer\n");
    
}
/**
 * size_from_channelarray() - calculate the storage size of a scan
 * @channels:		the channel info array
 * @num_channels:	number of channels
 *
 * Has the side effect of filling the channels[i].location values used
 * in processing the buffer output.
 **/
int size_from_channelarray(struct iio_channel_info *channels, int num_channels)
{
	int bytes = 0;
	int i = 0;

	while (i < num_channels) {
		if (bytes % channels[i].bytes == 0)
			channels[i].location = bytes;
		else
			channels[i].location = bytes - bytes % channels[i].bytes
					       + channels[i].bytes;

		bytes = channels[i].location + channels[i].bytes;
		i++;
	}

	return bytes;
}

int16_t get2byte(uint16_t input, struct iio_channel_info *info)
{
	/*
	 * Shift before conversion to avoid sign extension
	 * of left aligned data
	 */
	input >>= info->shift;
	input &= info->mask;
	return ((int16_t)(input << (16 - info->bits_used)) >>
			      (16 - info->bits_used));
}

int64_t getTimestampUs(uint64_t input, struct iio_channel_info *info)
{
	/*
	 * Shift before conversion to avoid sign extension
	 * of left aligned data
	 */
	input >>= info->shift;
	input &= info->mask;
	/* Convert to microseconds from nanoseconds before returning */
	return((int64_t)(input << (64 - info->bits_used)) >>
			      (64 - info->bits_used))/1000;
}


char *dev_dir_name = NULL;
char *buf_dir_name = NULL;
bool current_trigger_set = false;

void cleanup(void)
{
	int ret;
    printf("cleanup()\n");
	fflush(stdout);
	
	/* Disable buffer */
	if (buf_dir_name) {
		ret = write_sysfs_int("enable", buf_dir_name, 0);
		if (ret < 0)
			fprintf(stderr, "Failed to disable buffer: %s\n",
				strerror(-ret));
	}

	fflush(stdout);

	/* Disable trigger */
	if (dev_dir_name && current_trigger_set) {
		/* Disconnect the trigger - just write a dummy name. */
		ret = write_sysfs_string("trigger/current_trigger",
					 dev_dir_name, "NULL");
		if (ret < 0)
			fprintf(stderr, "Failed to disable trigger: %s\n",
				strerror(-ret));
		current_trigger_set = false;
	}
	fflush(stdout);


	/* Checkpoint data base to clean up -wal and -shm files */
	printf("call closeSqliteDB()\n"); 
	fflush(stdout);
	closeSqliteDB();
        
	fflush(stdout);

}

void sig_handler(int signum)
{
	fprintf(stderr, "Caught signal %d\n", signum);
	printf("stdout Caught signal %d\n", signum);
	cleanup();
	exit(-signum);
}

void register_cleanup(void)
{
	struct sigaction sa = { .sa_handler = sig_handler };
	const int signums[] = { SIGINT, SIGTERM, SIGABRT };
	int ret, i;

	for (i = 0; i < ARRAY_SIZE(signums); ++i) {
		ret = sigaction(signums[i], &sa, NULL);
		if (ret) {
			perror("Failed to register signal handler");
			exit(-1);
		}
	}
}

int main(int argc, char **argv)
{
	unsigned long buf_len;
	int buf_watermark;

	int ret, i, toread;
	int fp = -1;
	struct stat st = {0};

	int num_channels = 1;
	char *trigger_name = NULL, *device_name = NULL;

	char *data = NULL;
    char *element_name = NULL;
	ssize_t read_size;
	int dev_num = -1, trig_num = -1;
	char *buffer_access = NULL;
	int scan_size;
	char *trig_dev_name;

	char sqlDatabaseName[64];
	time_t nowSinceEpoch;
	struct tm *pNowDate;
	int currentDay;

	bool stopFileDoesNotExist;
    int stringLength;


	struct iio_channel_info *channels = NULL;

	register_cleanup();

	// Set up the current cape parameters
	//dev_num = 2;     // iio:device2
	trig_num = 1;    // trigger1 
	buf_len = NUMBER_SAMPLES_PER_DB_WRITE*2;    	// length of buffer
	buf_watermark = NUMBER_SAMPLES_PER_DB_WRITE;    // watermark in buffer
	
	
    while (1) {
        int c;

        
        static struct option long_options[]=
        {   
            {"device"          , required_argument,    0   ,'d'},
            {"element"         , required_argument,    0   ,'e'},
            {"sample-rate"     , required_argument,    0   ,'s'},
            {"trigger"         , no_argument,          0   ,'t'},
            {0,0,0,0}
        };
        
        int option_index = 0;
        
        c = getopt_long(argc,argv,"d:e:s:t", long_options, &option_index);
        
        if (c == -1)
            break;
        
        
        switch(c) {
            
            case 'd':
            {       stringLength = strlen(optarg);
                    pActualDevice = malloc(stringLength+1);
                    strcpy(pActualDevice,optarg);
                    break;
            }
            case 'e':
            {       stringLength = strlen(optarg);
                    pActualElement = malloc(stringLength+1);
                    strcpy(pActualElement,optarg);
                    break;
            }
            case 's':
            {       actualSampleRate = atoi(optarg);
                    break;
            }
            case 't':
            {       actualTrigger = true;
                    break;
            }

        }
    }
	
	
	// Set the default device name if not set by arguments
	if (pActualDevice == NULL)
    {
        stringLength = strlen(defaultDevice);
        pActualDevice = malloc(stringLength+1);
        strcpy(pActualDevice,defaultDevice);
    }

	// Set the default device name if not set by arguments
	if (pActualElement == NULL)
    {
        stringLength = strlen(defaultElement);
        pActualElement = malloc(stringLength+1);
        strcpy(pActualElement,defaultElement);
    }
    
    // Now find device number in IIO based on its name
	dev_num = find_type_by_name(pActualDevice, "iio:device");

	/* Create name for SQL db based on current time */
	nowSinceEpoch = time(NULL);
	pNowDate=gmtime(&nowSinceEpoch);
	if (pNowDate)
	{
		sprintf(sqlDatabaseName,"/var/www/sql/currentCapture-%02d-%02d-%02d-%02d:%02d:%02d",pNowDate->tm_mday, pNowDate->tm_mon+1, 
				pNowDate->tm_year+1900,pNowDate->tm_hour, pNowDate->tm_min, pNowDate->tm_sec);
		printf("File name %s\n",sqlDatabaseName);
            
		// Set the current day so we can use to check for new day
		currentDay=pNowDate->tm_mday;
            
	}
	else
	{
		printf("ERROR: Unable to create SQL database name\n");    
		return -1;
    }
           
	if (openSqliteDB(sqlDatabaseName) == 1)
	{
		printf("ERROR: Unable to open SQL database\n");    
		return -1;
	}
           

    if (actualTrigger)
    {
        /* Create the hr trigger for the sampling */
        if (stat("/sys/kernel/config/iio/triggers/hrtimer/trigger1", &st) == -1){ 
            mkdir("/sys/kernel/config/iio/triggers/hrtimer/trigger1",0700);
        }

        /* and set the sampling rate */
        ret = write_sysfs_int("sampling_frequency", "/sys/devices/trigger1", actualSampleRate);
        if (ret < 0) {
            fprintf(stderr, "Failed to write sampling rate\n");
            goto error;
        }
    }

	ret = asprintf(&dev_dir_name, "%siio:device%d", iio_dir, dev_num);
	if (ret < 0)
		return -ENOMEM;

	/* Fetch device_name if specified by number */
	device_name = malloc(IIO_MAX_NAME_LENGTH);
	if (!device_name) {
		ret = -ENOMEM;
		goto error;
	}
	ret = read_sysfs_string("name", dev_dir_name, device_name);
	if (ret < 0) {
		fprintf(stderr, "Failed to read name of device %d\n", dev_num);
		goto error;
	}
    
    if (actualTrigger)
    {
        ret = asprintf(&trig_dev_name, "%strigger%d", iio_dir, trig_num);
        if (ret < 0) {
            return -ENOMEM;
        }
        trigger_name = malloc(IIO_MAX_NAME_LENGTH);
        ret = read_sysfs_string("name", trig_dev_name, trigger_name);
        free(trig_dev_name);
        if (ret < 0) {
            fprintf(stderr, "Failed to read trigger%d name from\n", trig_num);
            return ret;
        }
    }
	/*
	 * Construct the directory name for the associated buffer.
	 * As we know that the ads1018 has only one buffer this may
	 * be built rather than found.
	 */
	ret = asprintf(&buf_dir_name,
		       "%siio:device%d/buffer", iio_dir, dev_num);
	if (ret < 0) {
		ret = -ENOMEM;
		goto error;
	}

   	/* Build element name to be enabled */
	element_name = malloc(IIO_MAX_NAME_LENGTH);
	if (!element_name) {
		ret = -ENOMEM;
		goto error;
	}

	sprintf(element_name,"scan_elements/in_%s_en",pActualElement);
    
	ret = write_sysfs_int(element_name, dev_dir_name, 1);
	if (ret < 0) {
		fprintf(stderr, "Failed to write %s\n",element_name);
		goto error;
	}
	ret = write_sysfs_int("scan_elements/in_timestamp_en", dev_dir_name, 1);
	if (ret < 0) {
		fprintf(stderr, "Failed to write in_timestamp_en\n");
		goto error;
	}

 	/*
	 * Parse the files in scan_elements to identify what channels are
	 * enabled in this device.
     * Must be done after element has been enabled.
	 */
	ret = build_channel_array(dev_dir_name, &channels, &num_channels);
 	if (ret) {
		fprintf(stderr, "Problem reading scan element information\n"
			"diag %s\n", dev_dir_name);
		goto error;
	}


	scan_size = size_from_channelarray(channels, num_channels);
	data = malloc(scan_size * buf_len);
	if (!data) {
		ret = -ENOMEM;
		goto error;
	}
    
    if (actualTrigger)
    {
        /*
        * Set the device trigger to be the data ready trigger found
        * above
        */
        ret = write_sysfs_string_and_verify("trigger/current_trigger",
                            dev_dir_name,
                            trigger_name);
        if (ret < 0) {
            fprintf(stderr,
                "Failed to write current_trigger file\n");
            goto error;
        }
    }

    /* Setup ring buffer parameters */
	ret = write_sysfs_int("length", buf_dir_name, buf_len);
	if (ret < 0)
		goto error;

	ret = write_sysfs_int("watermark", buf_dir_name, buf_watermark);
	if (ret < 0)
		goto error;

	/* Enable the buffer */
	ret = write_sysfs_int("enable", buf_dir_name, 1);
	if (ret < 0) {
		fprintf(stderr,
			"Failed to enable buffer: %s\n", strerror(-ret));
		goto error;
	}

	ret = asprintf(&buffer_access, "/dev/iio:device%d", dev_num);
	if (ret < 0) {
		ret = -ENOMEM;
		goto error;
	}


	/* Attempt to open non blocking the access dev */
	fp = open(buffer_access, O_RDONLY | O_NONBLOCK);
	if (fp == -1) { /* TODO: If it isn't there make the node */
		ret = -errno;
		fprintf(stderr, "Failed to open %s\n", buffer_access);
		goto error;
	}

	/* Check if stop file has been written */
	if (stat("/var/www/stopCapture", &st) == -1) 
		stopFileDoesNotExist=true;
	else
		stopFileDoesNotExist=false;	
        
    
	while (stopFileDoesNotExist) {
		char 		*pSampleData;
		uint64_t 	TSmicrosecond;
		uint16_t 	previousTimeUs;
		int16_t  	adcValue;


		struct pollfd pfd = {
			.fd = fp,
			.events = POLLIN,
		};

        // Check the current time
		nowSinceEpoch = time(NULL);
		pNowDate=gmtime(&nowSinceEpoch);
 
		if (currentDay != pNowDate->tm_mday)
		{
			// It is a new day so close current database 
			closeSqliteDB();
                    
			// and create a new one with a new name
			sprintf(sqlDatabaseName,"/var/www/sql/currentCapture-%02d-%02d-%02d-%02d:%02d:%02d",pNowDate->tm_mday, pNowDate->tm_mon+1, 
                        pNowDate->tm_year+1900,pNowDate->tm_hour, pNowDate->tm_min, pNowDate->tm_sec);
                    
			// and open it
			if (openSqliteDB(sqlDatabaseName) == 1)
			{
				printf("ERROR: Unable to open SQL database\n");    
				return -1;
            }
                   
			// Set the current day so we can use to check for new day on next loop
            currentDay=pNowDate->tm_mday;
                    
			}

                
		ret = poll(&pfd, 1, -1);
		if (ret < 0) {
			ret = -errno;
			goto error;
		} else if (ret == 0) {
			continue;
		}

		toread = buf_len;

 		// This will block until the watermark number of samples are available
		read_size = read(fp, data, toread * scan_size);

		if (read_size < 0) {
			if (errno == EAGAIN) {
				fprintf(stderr, "nothing available\n");
				continue;
			} else {
				break;
			}
		}
		
		// At this point we have two channels
		// a 2 byte channel with the 16 bit sample
		// an 8 byte channel with the timestamp in ns 
		for (i = 0; i < read_size / scan_size; i++) {
			pSampleData=data + scan_size * i;

			adcValue=get2byte(*(int16_t *)(pSampleData + channels[0].location),&channels[0]);
			TSmicrosecond=getTimestampUs(*(uint64_t *)(pSampleData + channels[1].location),&channels[1]);

			// If it is the first sample in the blob then populate the header values
			if (i==0)
			{
				currentSamples.startTime=TSmicrosecond;
				currentSamples.scalingFactor=SCALING_FACTOR;
				currentSamples.samplePeriod=SAMPLE_PERIOD_US;
				currentSamples.numberSamplesPerBlob=NUMBER_SAMPLES_PER_DB_WRITE;
				currentSamples.timeDeltaUs[i]=0;
				previousTimeUs=TSmicrosecond;
			}
			else
			{ 
				// Just store the value and the delta to previous samples
				currentSamples.timeDeltaUs[i]=(unsigned short) (TSmicrosecond-previousTimeUs);
				previousTimeUs=TSmicrosecond;
			}
			// Store actual data value
			currentSamples.ADCValues[i]=adcValue;
		}
           
           
		/* Write out the captured buffer to SQL */ 
		writeCurrentDataToSQL(currentSamples);

		/* Check if stop file has been written */
        if (stat("/var/www/stopCapture", &st) == -1) 
        {
			stopFileDoesNotExist=true;
		}
        else
        {
			stopFileDoesNotExist=false;
		}
	}


error:

	/* Disable the buffer */
    if (read_sysfs_posint("enable", buf_dir_name))
    {
        ret = write_sysfs_int("enable", buf_dir_name, 0);
        if (ret < 0) {
            fprintf(stderr,
                "Failed to disable buffer: %s\n", strerror(-ret));
            goto error;
        }
     }
    /* Disable the element that had been enabled so that another one could be enabled later */
    if (element_name && read_sysfs_posint(element_name, dev_dir_name))
    {
        ret = write_sysfs_int(element_name, dev_dir_name, 0);
        if (ret < 0) {
            fprintf(stderr, "Failed to disable %s\n",element_name);
        }
    }
	if (fp >= 0 && close(fp) == -1)
		perror("Failed to close buffer");
    
    // Close down the iio 
	cleanup();
   
    // Sleep for 1 second to allow IIO to stop
    usleep(1000000);
    fflush(stdout);
        
	checkBeforeFree(buffer_access);
	checkBeforeFree(data);
	checkBeforeFree(element_name);
	checkBeforeFree(buf_dir_name);
	for (i = num_channels - 1; i >= 0; i--) {
		checkBeforeFree(channels[i].name);
		checkBeforeFree(channels[i].generic_name);
	}
	checkBeforeFree(channels);
    if (actualTrigger) {
        checkBeforeFree(trigger_name);
    }
	checkBeforeFree(device_name);
	checkBeforeFree(dev_dir_name);
    
    checkBeforeFree(pActualDevice);

	return ret;
 
}


