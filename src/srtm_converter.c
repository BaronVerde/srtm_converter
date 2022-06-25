
/* Converts an srtm v 4.1 ascii file with height values to a series of textures
 * in png format. Writes out the texture and two ascii files that describe the
 * axis aligned bounding boxes of each tile. One bb is relative to the texture,
 * starting in the lower left corner, the other relative to the given oblate
 * ellipsoid in geodetic (lat/lon) decimal notation.
 * Parameters:
 * - pathname of ascii file to import
 * - size of texture tiles to generate, default is 2048
 * - ellipsoid semi major axes (x/y equatorial plane), default WGS84
 * - ellipsoid semi minor axes (z = rotation axis), default WGS84 */

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <png.h>
#include "omath/ellipsoid.h"
#include "omath/common.h"
#include <tgmath.h>
#include <string.h>

// Header info of an ascii srtm-90 file, the only input format
typedef struct srtm_header_t {
	// humber of posts in width and height
	uint32_t num_columns;
	uint32_t num_rows;
	// lower left corner
	double longitude;
	double latitude;
	// distance between posts; angle in arcseconds decimel
	double cellsize;
	// a default when there's no data for a post
	int no_data;
	uint32_t tilesize;
} srtm_header_t;

bool read_srtm_ascii_header( FILE *file, srtm_header_t *header ) {
	// Read header data of an srtm v 4.1 file
	if( 1 == fscanf( file, "ncols %u\n", &header->num_columns ) )
		printf( "Columns: %d\n", header->num_columns );
	else {
		fputs( "Error reading ascii header number of columns\n", stderr );
		return false;
	}
	if( 1 == fscanf( file, "nrows %u\n", &header->num_rows ) )
		printf( "Rows: %d\n", header->num_rows );
	else {
		fputs( "Error reading ascii header number of rows\n", stderr );
		return false;
	}
	if( header->num_columns <= 0 || header->num_rows <= 0 ||
		header->num_columns < header->tilesize || header->num_rows < header->tilesize ) {
		fputs( "Error, size of data could not be determined or tile size > size of data\n", stderr );
		return false;
	}
	if( 1 == fscanf( file, "xllcorner %lf\n", &header->longitude ) )
		printf( "Lower left lon: %lf", header->longitude );
	else {
		fputs( "Error reading ascii header lower left x (longitude)\n", stderr );
		return false;
	}
	if( 1 == fscanf( file, "yllcorner %lf\n", &header->latitude ) )
		printf( "; lat: %lf\n", header->latitude );
	else {
		fputs( "Error reading ascii header lower left y (latitude)\n", stderr );
		return false;
	}
	if( header->longitude < -180.0 || header->latitude < -90.0 ||
		header->longitude > 180.0 || header->latitude > 90.0 ) {
		fputs( "Error in latitude or longitude; out of bounds\n", stderr );
		return false;
	}
	if( 1 == fscanf( file, "cellsize %lf\n", &header->cellsize ) )
		printf( "Cellsize: %lf arcsec\n", header->cellsize );
	else {
		fputs( "Cellsize could not be determined\n", stderr );
		return false;
	}
	if( 1 == fscanf( file, "NODATA_value %d\n", &header->no_data ) )
		printf( "No data value: %d\n", header->no_data );
	else {
		fputs( "Error reading no data value\n", stderr );
		return false;
	}
	return true;
}

// converts degrees decimal to degrees minutes arcseconds
static inline void deg2dms( const double dec, uint32_t *deg, uint32_t *min, uint32_t *sec ) {
	*deg = (uint32_t)floor(dec);
	double rest_mins = dec - floor(dec);
	*min = (uint32_t)floor(rest_mins*60.0);
	double rest_secs = rest_mins - floor(rest_mins*60.0);
	*sec = (uint32_t)(rest_secs*60.0);
}

void read_image( uint16_t ***image_data, srtm_header_t *header, FILE *in_file ) {
	// Read whole image into array
	puts("Reading image data ...");
	*image_data = malloc(header->num_rows*sizeof(uint16_t *));
	int value;
	int min_value = INT_MAX;
	int max_value = INT_MIN;
	int value_count = 0;
	uint32_t num_values = header->num_columns * header->num_rows;
	for( uint32_t i = 0; i < header->num_rows; ++i ) {
		(*image_data)[i] = malloc(header->num_columns*sizeof(uint16_t*));
		for( uint32_t j = 0; j < header->num_columns; ++j ) {
			fscanf( in_file, "%d", &value );
			/* Set no data to 0 but this can cause holes in some areas where there is a no data value,
			 * e.g. on some glaciers or where it was particularly cloudy, which happens in the srtm data.
			 * Also clip negative values to 0; it is often sea surface.
			 * Real negative height values below the reference ellipsoid's surface are excluded. */
			min_value = min_value < value ? min_value : value;
			max_value = max_value > value ? max_value : value;
			(*image_data)[i][j] = value < 0 || value == header->no_data ? 0 : (uint16_t)value;
			++value_count;
		}
	}
	printf("Read %d of %d value; min %d; max %d\n", value_count, num_values, min_value, max_value );
}

void free_image_data( uint16_t **image_data, srtm_header_t *header ) {
	for( uint32_t i = 0; i < header->num_rows; ++i )
		free(image_data[i]);
	free(image_data);
}

/* Calculate start rows/columns for each tile. The last column/top row must overlap so that the new one
 * starts on the row/column on which the old one ended or there will be gaps between tiles when rendering.
 * @fixme: Apart from this tiles should overlap by 1 on each side because of normal calculation from
 * averaging over adjacent posts and sobel filtering. See shaders of terrain lod. */
void write_tile( const uint32_t tile, const srtm_header_t *const header,
		const uint32_t *const start_row, const uint32_t *const start_col,
		uint16_t *const *const image_data, uint16_t **image ) {
	uint16_t min_y = 65535;
	uint16_t max_y = 0;
	for( uint32_t row = start_row[tile]; row < start_row[tile]+header->tilesize; ++row ) {
		for( uint32_t col = start_col[tile]; col < start_col[tile]+header->tilesize; ++col ) {
			const uint16_t value = image_data[col][row];
			image[col-start_col[tile]][row-start_row[tile]] = value;
			min_y = min_y > value ? value : min_y;
			max_y = max_y < value ? value : max_y;
		}
	}
	char filename[30] = "tile_";
	sprintf( &filename[5], "%u", header->tilesize );
	sprintf( &filename[9], "_%u.png", tile+1 );
	// print writing image x of y
	printf( "Writing image file '%s'\n", filename );
	png_structp png_stru = png_create_write_struct( PNG_LIBPNG_VER_STRING, NULL, NULL, NULL );
	if( !png_stru ) {
		fputs( "Error creating png struct", stderr );
		return;
	}
	png_infop png_inf = png_create_info_struct( png_stru );
	if( !png_inf ) {
		fputs( "Error creating png info struct", stderr );
		png_destroy_write_struct( &png_stru, &png_inf );
		return;
	}
	FILE *image_file = fopen( filename, "wb" );
	png_init_io( png_stru, image_file );
	png_set_IHDR(
			png_stru, png_inf, header->tilesize, header->tilesize, 16, PNG_COLOR_TYPE_GRAY,
			PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT
	);
	png_write_info( png_stru, png_inf );
	// Mind endianess
	png_set_swap( png_stru );
	for( uint32_t i = 0; i < header->tilesize; ++i )
		png_write_row( png_stru, (png_const_bytep)&image[i][0] );
	png_write_end( png_stru, png_inf );
	//png_destroy_write_struct( &png_stru, (png_infopp)NULL );
	png_destroy_write_struct( &png_stru, &png_inf );
	fclose(image_file);
	// Axis aligned bounding boxes, overwrite ending of filename (1 letter less than be4)
	sprintf( &filename[strlen(filename)-4], ".bb" );
	FILE *bb_file = fopen( filename, "w" );
	// Relative to input data (beginning 0/0/0), used to calculate texture positions during rendering
	const uint32_t min_x = start_col[tile];
	const uint32_t min_z = start_row[tile];
	const uint32_t max_x = header->tilesize - 1 + start_col[tile];
	const uint32_t max_z = header->tilesize - 1 + start_row[tile];
	fprintf( bb_file, "%u %u %u %u %u %u\n", min_x, min_y, min_z, max_x, max_y, max_z );
	printf( "\trelative aabb (%u/%u/%u)/(%u/%u/%u)\n", min_x, min_y, min_z, max_x, max_y, max_z );
	/* @todo Construct real world bounding box minimum and maximum from geodetic coordinates as follows:
	 * geodetic lower left x + startColumn[tileNumber] * geodetic cellsize,
	 * minimum height,
	 * geodetic lower left y + startRow[tileNumber] * geodetic cellsize,
	 * geodetic lower left x + startColumn[tileNumber] * geodetic cellsize + (tilesize-1) * geodetic cellsize,
	 * maximum height,
	 * geodetic lower left y + startRow[tileNumber] * geodetic cellsize + (tilesize-1) * geodetic cellsize */
	const double min_lon = header->longitude + (double)start_col[tile] * header->cellsize;
	const double min_lat = header->latitude + (double)start_row[tile] * header->cellsize;
	// Write minimum lon and lat for later caclculation of world coords
	fprintf( bb_file, "%lf %lf %lf\n", min_lon, min_lat, header->cellsize );
	printf( "\tlower left geodetic coords: lon %lf lat %lf cellsize %lf\n", min_lon, min_lat, header->cellsize );
	fclose(bb_file);
}

int main( int argc, char *argv[argc+1] ) {
	puts("Converter starting ...");
	uint32_t tilesize = 2048;
	double semi_major = 6378137.0;
	double semi_minor = 6356752.314245;
	char *temp;
	if( argc > 2 ) {
		tilesize = (uint32_t)strtoimax( argv[2], &temp, 10 );
		if( !is_pow2u( tilesize ) || tilesize < 256 || tilesize > 16384 ) {
			fprintf( stderr, "Tilesize must be power of 2 and between 256 and 16384, is '%s'\n", argv[3] );
			return EXIT_FAILURE;
		}
	}
	if( argc == 5 ) {
		semi_major = strtod( argv[3], &temp );
		if( semi_major <= 0.0 ) {
			fprintf( stderr, "Semi major axis must be > 0.0, is '%s'\n", argv[3] );
			return EXIT_FAILURE;
		}
		semi_minor = strtod( argv[4], &temp );
		if( semi_minor <= 0.0 || semi_minor > semi_major ) {
			fprintf( stderr, "Semi minor axis must be > 0.0 and smaller than semi major axes, is '%s'\n", argv[4] );
			return EXIT_FAILURE;
		}
	} else if( argc != 3 ) {
		fprintf( stderr, "Usage: '%s <ascii input file> <tilesize> <semi major axes> <semi minor axis>\n", argv[0] );
		return EXIT_FAILURE;
	}
	FILE *in_file;
	if( !(in_file = fopen( argv[1], "r" )) ) {
		fprintf( stderr, "Error opening input file '%s'\n", argv[1] );
		return EXIT_FAILURE;
	}
	printf( "Converting '%s':\nTilesize %d\nEllipsoid (%lf/%lf)\n\n", argv[1], tilesize, semi_major, semi_minor );
	srtm_header_t in_header;
	in_header.tilesize = tilesize;
	if( !read_srtm_ascii_header( in_file, &in_header ) )
		fputs( "Error reading image header", stderr );
	else {
		// convert lower left to cartesian
		ellipsoid_t eps;
		ellipsoid_create( semi_major, semi_major, semi_minor, &eps );
		vec3d ll_cart;
		const geodetic_t ll_geo = { in_header.longitude, in_header.latitude, 0.0 };
		ellipsoid_to_cartesian( &ll_geo, &eps, &ll_cart );
		printf( "\nLower left in cartesian coords: (%lf/%lf/%lf)\n", ll_cart.x, ll_cart.y, ll_cart.z );
		uint16_t **image_data = NULL;
		read_image( &image_data, &in_header, in_file );
		fclose(in_file);
		// Convert images
		puts("Converting images ...");
		// Filet the map into tiles starting at row/col
		const uint32_t num_h_tiles = (uint32_t)floor(in_header.num_columns/tilesize);
		const uint32_t num_v_tiles = (uint32_t)floor(in_header.num_rows/tilesize);
		printf( "Number of tiles horizontal/vertical: %d/%d\n", num_h_tiles, num_v_tiles );
		const uint32_t num_tiles = num_h_tiles * num_v_tiles;
		uint32_t start_row[num_tiles];
		uint32_t start_col[num_tiles];
		int k = 0;
		for( uint32_t i = 0; i < num_h_tiles; ++i ) {
			for( uint32_t j = 0; j < num_v_tiles; ++j ) {
				// @todo is this right ? 1 overlap ?
				start_row[k] = i*(tilesize-1);
				start_col[k] = j*(tilesize-1);
				printf("\tTile %d, starting at col/row %d/%d\n", k, start_col[k], start_row[k] );
				++k;
			}
		}
		// Make room for tile data
		uint16_t **image;
		image = malloc(sizeof(uint16_t*)*tilesize);
		for( uint32_t row = 0; row < tilesize; ++row )
			image[row] = malloc(sizeof(uint16_t)*tilesize);
		// copy over the tile window from the image data, determine min/max values
		for( uint32_t tile = 0; tile < num_tiles; ++tile )
			write_tile( tile, &in_header, start_row, start_col, image_data, image );
		// cleanup
		for( uint32_t row = 0; row < tilesize; ++row )
			free(image[row]);
		free(image);
		free_image_data( image_data, &in_header );
	}
	puts("\nConverter ending.");
	return EXIT_SUCCESS;
}
