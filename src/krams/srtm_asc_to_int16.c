
/* Convert an srtm asc file as a whole to int16
 * No error checking is done here for now
 * Temporary hack for 6 srtm 90m 30° by 30° tiles, 3 wide, 2 high */

#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>

#define MAX_LEN 200

// Keep the order or bad things will happen ...
const char *in_filenames[6] = {
		"data/big/cut_n00e030.asc", "data/big/cut_n00e060.asc", "data/big/cut_n00e090.asc",
		"data/big/cut_n30e030.asc", "data/big/cut_n30e060.asc", "data/big/cut_n30e090.asc"
};

FILE *in_files[6];

typedef struct srtm_header_t {
	uint32_t num_columns;
	uint32_t num_rows;
	double longitude;
	double latitude;
	double cellsize;
	int no_data;
} srtm_header_t;

bool convert_header( const unsigned int i ) {
	in_files[i] = fopen( in_filenames[i], "r" );
	if( !in_files[i] ) {
		fprintf( stderr, "Could not open '%s'\n", in_filenames[i] );
		return false;
	}
	srtm_header_t header;
	int res = 0;
	res += fscanf( in_files[i], "ncols %u\n", &header.num_columns );
	res += fscanf( in_files[i], "nrows %u\n", &header.num_rows );
	res += fscanf( in_files[i], "xllcorner %lf\n", &header.longitude );
	res += fscanf( in_files[i], "yllcorner %lf\n", &header.latitude );
	res += fscanf( in_files[i], "cellsize %lf\n", &header.cellsize );
	res += fscanf( in_files[i], "NODATA_value %d\n", &header.no_data );
	if( 6 != res ) {
		fprintf( stderr, "Could not read header info from '%s'\n", in_filenames[i] );
		return false;
	}
	char msg[MAX_LEN];
	snprintf( msg, MAX_LEN-1, "File '%s':\n\tcols/rows: %u/%u\n\tll corner lon/lat: %g/%g\n\tcellsize %g NO DATA %d\n",
			in_filenames[i], header.num_columns, header.num_rows, header.longitude, header.latitude,
			header.cellsize, header.no_data );
	fputs(msg, stdout);
	// write header
	snprintf( msg, MAX_LEN-1, "%s.hdr", in_filenames[i] );
	FILE *out_hdr = fopen( msg, "wb" );
	fwrite( &header, sizeof(srtm_header_t), 1, out_hdr );
	fclose(out_hdr);
	return true;
}

void close_all() {
	for( unsigned int i=0; i<6; ++i )
		if( in_files[i] )
			fclose(in_files[i]);
}

int main(void) {
	const uint32_t total_rows=2*36023;
	const uint32_t total_cols=3*36023;
	for( unsigned int i=0; i<6; ++i )
		if( !convert_header(i) ) {
			printf( "'%s' error converting\n", in_filenames[i] );
			close_all();
			return EXIT_FAILURE;
		}

	const double total_size = (double)(total_rows*total_cols*sizeof(int16_t))/1024.0;
	printf( "Total rows %u, cols %u, output file size %.2fKiB\n", total_rows, total_cols, total_size );

	/*int16_t one_row[total_cols];
	int value;
	FILE *out_file = fopen( "data/cut/asia.int16", "wb" );
	for( uint32_t row = 0; row < total_rows; ++row ) {
		for( uint32_t col = 0; col < total_cols; ++col ) {
			fscanf( in_file, "%d", &value );
			one_row[col] = (int16_t)value;
		}
		fwrite( one_row, header.num_columns*sizeof(int16_t), 1, out_file );
	}
	fclose(in_file);
	fclose(out_file);*/
	close_all();
	return EXIT_SUCCESS;
}
