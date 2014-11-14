#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <tempered.h>
#include <getopt.h>
#include <tempered-util.h>

struct my_options {
	bool enumerate;
	bool batch;
	int calibtemp_count;
	float * calibtemp_values;
	int calibrelh_count;
	float * calibrelh_values;
	char ** devices;
};

void free_options( struct my_options *options )
{
	free( options->calibtemp_values );
	free( options->calibrelh_values );
	// Entries of options->devices are straight from argv, so don't free() them.
	free( options->devices );
	free( options );
}

void show_help()
{
	printf(
"Usage: tempered [options] [device-path...]\n"
"\n"
"Known options:\n"
"    -h\n"
"    --help                 Show this help text\n"
"    -e\n"
"    --enumerate            Enumerate the found devices without reading them.\n"
"    -b\n"
"    --batch                Use batch output format.\n"
"    -c <cal>\n"
"    --calibrate-temp <cal> Calibrate the measured temperature.\n"
"    -r <cal>\n"
"    --calibrate-relh <cal> Calibrate the measured relative humidity.\n"
	);
}

struct my_options* parse_options( int argc, char *argv[] )
{
	struct my_options options = {
		.enumerate = false,
		.batch = false,
		.calibtemp_count = 0,
		.calibtemp_values = NULL,
		.calibrelh_count = 0,
		.calibrelh_values = NULL,
		.devices = NULL,
	};
	char *calibtemp_string = NULL;
	char *calibrelh_string = NULL;
	struct option const long_options[] = {
		{ "help", no_argument, NULL, 'h' },
		{ "enumerate", no_argument, NULL, 'e' },
		{ "batch", no_argument, NULL, 'b' },
		{ "calibrate-temp", required_argument, NULL, 'c' },
		{ "calibrate-relh", required_argument, NULL, 'r' },
		{ NULL, 0, NULL, 0 }
	};
	char const * const short_options = "hebs:c:r:";
	while ( true ) {
		int opt = getopt_long( argc, argv, short_options, long_options, NULL );
		if ( opt == -1 ) {
			break;
		}
		switch ( opt ) {
			case 0:// This should never happen since all options have flag==NULL
			default:
				fprintf( stderr, "Error: invalid option found." );
				return NULL;
				break;
			case '?':
				return NULL;
				break;
			case 'h':
				show_help();
				return NULL;
				break;
			case 'e':
				options.enumerate = true;
				break;
			case 'b':
				options.batch = true;
				break;
			case 'c':
				calibtemp_string = optarg;
				break;
			case 'r':
				calibrelh_string = optarg;
				break;
		}
	}
	if ( calibtemp_string != NULL ) {
		options.calibtemp_values = tempered_util__parse_calibration_string( calibtemp_string, &(options.calibtemp_count), true);
		if ( options.calibtemp_values == NULL ) {
			return NULL;
		}
	}
	if ( calibrelh_string != NULL ) {
		options.calibrelh_values = tempered_util__parse_calibration_string( calibrelh_string, &(options.calibrelh_count), true);
		if ( options.calibrelh_values == NULL ) {
			return NULL;
		}
	}
	if ( optind < argc ) {
		int count = argc - optind;
		char **devices = calloc( count + 1, sizeof( char* ) );
		memcpy( devices, &(argv[optind]), count * sizeof( char* ) );
		options.devices = devices;
	}
	struct my_options * heap_options = malloc( sizeof( struct my_options ) );
	memcpy( heap_options, &options, sizeof( struct my_options ) );
	return heap_options;
}

/** Get and print the sensor values for a given device and sensor. */
void print_device_sensor( tempered_device *device, int sensor, struct my_options *options) {
	float tempC, rel_hum;
	int type = tempered_get_sensor_type( device, sensor );
	const char* dev_path = tempered_get_device_path(device);
	if ( type & TEMPERED_SENSOR_TYPE_TEMPERATURE ) {
		if ( !tempered_get_temperature( device, sensor, &tempC ) ) {
			fprintf( stderr, "%s %i: Failed to get the temperature: %s\n", dev_path, sensor, tempered_error( device ));
			type &= ~TEMPERED_SENSOR_TYPE_TEMPERATURE;
		}
		else if ( options->calibtemp_values != NULL ) {
			tempC = tempered_util__calibrate_value( tempC, options->calibtemp_count, options->calibtemp_values);
		}
	}
	if ( type & TEMPERED_SENSOR_TYPE_HUMIDITY ) {
		if ( !tempered_get_humidity( device, sensor, &rel_hum ) ) {
			fprintf( stderr, "%s %i: Failed to get the humidity: %s\n", dev_path, sensor, tempered_error( device ));
			type &= ~TEMPERED_SENSOR_TYPE_HUMIDITY;
		}
		else if ( options->calibrelh_values != NULL ) {
			rel_hum = tempered_util__calibrate_value( rel_hum, options->calibrelh_count, options->calibrelh_values);
		}
	}
	if ( ( type & TEMPERED_SENSOR_TYPE_TEMPERATURE ) && ( type & TEMPERED_SENSOR_TYPE_HUMIDITY )) {
		if(options->batch) {
			printf( "'%s', %.2f, %.2f, %.2f\n", dev_path, tempC, rel_hum, tempered_util__get_dew_point( tempC, rel_hum ));
		}
		else {
			printf( "%s %i: temperature %.2f C" ", relative humidity %.1f%%" ", dew point %.1f C\n",
				dev_path, sensor, tempC, rel_hum, tempered_util__get_dew_point( tempC, rel_hum )
			);
		}
	}
	else if ( type & TEMPERED_SENSOR_TYPE_TEMPERATURE ) {
		printf( "%s %i: temperature %.2f C\n", dev_path, sensor, tempC);
	}
	else if ( type & TEMPERED_SENSOR_TYPE_HUMIDITY ) {
		printf( "%s %i: relative humidity %.1f%%\n", dev_path, sensor, rel_hum);
	}
	else {
		printf( "%s %i: no sensor data available\n", dev_path, sensor);
	}
}

/** Print the sensor values for a given device. */
void print_device( struct tempered_device_list *dev, struct my_options *options) {
	if ( options->enumerate ) {
		printf( "%s : %s (USB IDs %04X:%04X)\n", dev->path, dev->type_name, dev->vendor_id, dev->product_id);
		return;
	}
	char *error = NULL;
	tempered_device *device = tempered_open( dev, &error );
	if ( device == NULL ) {
		fprintf( stderr, "%s: Could not open device: %s\n", dev->path, error);
		free( error );
		return;
	}
	if ( !tempered_read_sensors( device ) ) {
		fprintf( stderr, "%s: Failed to read the sensors: %s\n", tempered_get_device_path( device ), tempered_error( device ));
	}
	else
	{
		int sensor, sensors = tempered_get_sensor_count( device );
		for ( sensor = 0; sensor < sensors; sensor++ ) {
			print_device_sensor( device, sensor, options );
		}
	}
	tempered_close( device );
}

int main( int argc, char *argv[] )
{
	struct my_options *options = parse_options( argc, argv );
	if ( options == NULL ) {
		return 1;
	}
	char *error = NULL;
	if ( !tempered_init( &error ) ) {
		fprintf( stderr, "Failed to initialize libtempered: %s\n", error );
		free( error );
		free_options( options );
		return 1;
	}
	
	struct tempered_device_list *list = tempered_enumerate( &error );
	if ( list == NULL ) {
		fprintf( stderr, "Failed to enumerate devices: %s\n", error );
		free( error );
	}
	else {
		if ( options->devices != NULL ) {
			// We have parameters, so only print those devices that are given.
			int i;
			for ( i = 0; options->devices[i] != NULL ; i++ ) {
				bool found = false;
				struct tempered_device_list *dev;
				for ( dev = list ; dev != NULL ; dev = dev->next ) {
					if ( strcmp( dev->path, options->devices[i] ) == 0 ) {
						found = true;
						print_device( dev, options );
						break;
					}
				}
				if ( !found ) {
					fprintf( stderr, "%s: TEMPered device not found or ignored.\n", options->devices[i]);
				}
			}
		}
		else {
			// We don't have any parameters, so print all the devices we found.
			struct tempered_device_list *dev;
			for ( dev = list ; dev != NULL ; dev = dev->next ) {
				print_device( dev, options );
			}
		}
		tempered_free_device_list( list );
	}
	
	if ( !tempered_exit( &error ) ) {
		fprintf( stderr, "%s\n", error );
		free( error );
		free_options( options );
		return 1;
	}
	return 0;
}
/* vim: set tabstop=4 : */
