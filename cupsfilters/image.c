//
// Base image support for libcupsfilters.
//
// Copyright 2007-2011 by Apple Inc.
// Copyright 1993-2005 by Easy Software Products.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//
// Contents:
//
//   cfImageClose()         - Close an image file.
//   cfImageGetCol()        - Get a column of pixels from an image.
//   cfImageGetColorSpace() - Get the image colorspace.
//   cfImageGetDepth()      - Get the number of bytes per pixel.
//   cfImageGetHeight()     - Get the height of an image.
//   cfImageGetRow()        - Get a row of pixels from an image.
//   cfImageGetWidth()      - Get the width of an image.
//   cfImageGetXPPI()       - Get the horizontal resolution of an image.
//   cfImageGetYPPI()       - Get the vertical resolution of an image.
//   cfImageOpen()          - Open an image file and read it into memory.
//   _cfImagePutCol()       - Put a column of pixels to an image.
//   _cfImagePutRow()       - Put a row of pixels to an image.
//   cfImageSetMaxTiles()   - Set the maximum number of tiles to cache.
//   cfImageCrop()          - Crop an image.
//   flush_tile()           - Flush the least-recently-used tile in the cache.
//   get_tile()             - Get a cached tile.
//   _cfImageReadEXIF()     - to read exif metadata of images
//   trim_spaces()          - helper function to extract results from string 
//                            returned by exif library functions
//   find_bytes()           - creates character array from image file, to make
//                            use in exif library functions


//
// Include necessary headers...
//


#include "image-private.h"
#include "config.h"

#ifdef HAVE_LIBJXL
#include <jxl/decode.h>
#include "image-jpeg-xl.h"
#endif


//
// Local functions...
//

static int	flush_tile(cf_image_t *img);
static cf_ib_t	*get_tile(cf_image_t *img, int x, int y);
#ifdef HAVE_EXIF
static void trim_spaces(char *buf);
static unsigned char *find_bytes(FILE *fp, long int *size);
#endif // HAVE_EXIF

//
// 'cfImageClose()' - Close an image file.
//

void
cfImageClose(cf_image_t *img)		// I - Image to close
{
  cf_ic_t	*current,		// Current cached tile
		*next;			// Next cached tile


  //
  // Wipe the tile cache file (if any)...
  //

  if (img->cachefile >= 0)
  {
    DEBUG_printf(("Closing/removing swap file \"%s\"...\n", img->cachename));

    close(img->cachefile);
    unlink(img->cachename);
  }

  //
  // Free the image cache...
  //

  DEBUG_puts("Freeing memory...");

  for (current = img->first, next = NULL; current != NULL; current = next)
  {
    DEBUG_printf(("Freeing cache (%p, next = %p)...\n", current, next));

    next = current->next;
    free(current);
  }

  //
  // Free the rest of memory...
  //

  if (img->tiles != NULL)
  {
    DEBUG_printf(("Freeing tiles (%p)...\n", img->tiles[0]));

    free(img->tiles[0]);

    DEBUG_printf(("Freeing tile pointers (%p)...\n", img->tiles));

    free(img->tiles);
  }

  free(img);
}


//
// 'cfImageGetCol()' - Get a column of pixels from an image.
//

int					// O - -1 on error, 0 on success
cfImageGetCol(cf_image_t   *img,	// I - Image
	      int          x,		// I - Column
	      int          y,		// I - Start row
	      int          height,	// I - Column height
	      cf_ib_t      *pixels)	// O - Pixel data
{
  int			bpp,		// Bytes per pixel
			twidth,		// Tile width
			count;		// Number of pixels to get
  const cf_ib_t		*ib;		// Pointer into tile


  if (img == NULL || x < 0 || x >= img->xsize || y >= img->ysize)
    return (-1);

  if (y < 0)
  {
    height += y;
    y = 0;
  }

  if ((y + height) > img->ysize)
    height = img->ysize - y;

  if (height < 1)
    return (-1);

  bpp    = cfImageGetDepth(img);
  twidth = bpp * (CF_TILE_SIZE - 1);

  while (height > 0)
  {
    ib = get_tile(img, x, y);

    if (ib == NULL)
      return (-1);

    count = CF_TILE_SIZE - (y & (CF_TILE_SIZE - 1));
    if (count > height)
      count = height;

    y      += count;
    height -= count;

    for (; count > 0; count --, ib += twidth)
      switch (bpp)
      {
        case 4 :
            *pixels++ = *ib++;
        case 3 :
            *pixels++ = *ib++;
            *pixels++ = *ib++;
        case 1 :
            *pixels++ = *ib++;
            break;
      }
  }

  return (0);
}


//
// 'cfImageGetColorSpace()' - Get the image colorspace.
//

cf_icspace_t				// O - Colorspace
cfImageGetColorSpace(
    cf_image_t *img)			// I - Image
{
  return (img->colorspace);
}


//
// 'cfImageGetDepth()' - Get the number of bytes per pixel.
//

int					// O - Bytes per pixel
cfImageGetDepth(cf_image_t *img)	// I - Image
{
  return (abs(img->colorspace));
}


//
// 'cfImageGetHeight()' - Get the height of an image.
//

unsigned				// O - Height in pixels
cfImageGetHeight(cf_image_t *img)	// I - Image
{
  return (img->ysize);
}


//
// 'cfImageGetRow()' - Get a row of pixels from an image.
//

int					// O - -1 on error, 0 on success
cfImageGetRow(cf_image_t   *img,	// I - Image
	      int          x,		// I - Start column
	      int          y,		// I - Row
	      int          width,	// I - Width of row
	      cf_ib_t      *pixels)	// O - Pixel data
{
  int			bpp,		// Bytes per pixel
			count;		// Number of pixels to get
  const cf_ib_t		*ib;		// Pointer to pixels


  if (img == NULL || y < 0 || y >= img->ysize || x >= img->xsize)
    return (-1);

  if (x < 0)
  {
    width += x;
    x = 0;
  }

  if ((x + width) > img->xsize)
    width = img->xsize - x;

  if (width < 1)
    return (-1);

  bpp = img->colorspace < 0 ? -img->colorspace : img->colorspace;

  while (width > 0)
  {
    ib = get_tile(img, x, y);

    if (ib == NULL)
      return (-1);

    count = CF_TILE_SIZE - (x & (CF_TILE_SIZE - 1));
    if (count > width)
      count = width;
    memcpy(pixels, ib, count * bpp);
    pixels += count * bpp;
    x      += count;
    width  -= count;
  }

  return (0);
}


//
// 'cfImageGetWidth()' - Get the width of an image.
//

unsigned				// O - Width in pixels
cfImageGetWidth(cf_image_t *img)	// I - Image
{
  return (img->xsize);
}


//
// 'cfImageGetXPPI()' - Get the horizontal resolution of an image.
//

unsigned				// O - Horizontal PPI
cfImageGetXPPI(cf_image_t *img)		// I - Image
{
  return (img->xppi);
}


//
// 'cfImageGetYPPI()' - Get the vertical resolution of an image.
//

unsigned				// O - Vertical PPI
cfImageGetYPPI(cf_image_t *img)		// I - Image
{
  return (img->yppi);
}


//
// 'cfImageOpen()' - Open an image file and read it into memory.
//

cf_image_t *				// O - New image
cfImageOpen(
    const char      *filename,		// I - Filename of image
    cf_icspace_t    primary,		// I - Primary colorspace needed
    cf_icspace_t    secondary,		// I - Secondary colorspace if primary
                                        //     no good
    int             saturation,		// I - Color saturation level
    int             hue,		// I - Color hue adjustment
    const cf_ib_t   *lut)		// I - RGB gamma/brightness LUT
{
  FILE		*fp;			// File pointer

  DEBUG_printf(("cfImageOpen(\"%s\", %d, %d, %d, %d, %p)\n",
		filename ? filename : "(null)", primary, secondary,
		saturation, hue, lut));

  if ((fp = fopen(filename, "rb")) == NULL)
    return (NULL);

  return (cfImageOpenFP(fp, primary, secondary, saturation, hue, lut));
}


//
// 'cfImageOpenFP()' - Open an image file and read it into memory.
//

cf_image_t *				// O - New image
cfImageOpenFP(
    FILE            *fp,		// I - File pointer of image
    cf_icspace_t    primary,		// I - Primary colorspace needed
    cf_icspace_t    secondary,		// I - Secondary colorspace if primary
                                        //     no good
    int             saturation,		// I - Color saturation level
    int             hue,		// I - Color hue adjustment
    const cf_ib_t   *lut)		// I - RGB gamma/brightness LUT
{
  unsigned char	header[16],		// First 16 bytes of file
		header2[16];		// Bytes 2048-2064 (PhotoCD)
  cf_image_t	*img;			// New image buffer
  int		status;			// Status of load...


  DEBUG_printf(("cfImageOpen2(%p, %d, %d, %d, %d, %p)\n",
        	fp, primary, secondary, saturation, hue, lut));

  //
  // Figure out the file type...
  //

  if (fp == NULL)
    return (NULL);

  if (fread(header, 1, sizeof(header), fp) == 0)
  {
    fclose(fp);
    return (NULL);
  }

  fseek(fp, 2048, SEEK_SET);
  memset(header2, 0, sizeof(header2));
  if (fread(header2, 1, sizeof(header2), fp) == 0 && ferror(fp))
    DEBUG_printf(("Error reading file!"));
  fseek(fp, 0, SEEK_SET);

  //
  // Allocate memory...
  //

  img = calloc(1, sizeof(cf_image_t));

  if (img == NULL)
  {
    fclose(fp);
    return (NULL);
  }

  //
  // Load the image as appropriate...
  //

  img->cachefile = -1;
  img->max_ics   = CF_TILE_MINIMUM;
  img->xppi      = 200;
  img->yppi      = 200;

#ifdef HAVE_LIBPNG
  if (!memcmp(header, "\211PNG", 4))
    status = _cfImageReadPNG(img, fp, primary, secondary, saturation, hue,
			     lut);
  else
#endif // HAVE_LIBPNG
#ifdef HAVE_LIBJPEG
  if (!memcmp(header, "\377\330\377", 3) &&	// Start-of-Image
      header[3] >= 0xe0 && header[3] <= 0xef)	// APPn
    status = _cfImageReadJPEG(img, fp, primary, secondary, saturation, hue,
			      lut);
  else
#endif // HAVE_LIBJPEG
#ifdef HAVE_LIBTIFF
  if (!memcmp(header, "MM\000\052", 4) ||
      !memcmp(header, "II\052\000", 4))
    status = _cfImageReadTIFF(img, fp, primary, secondary, saturation, hue,
			      lut);
  else
#endif // HAVE_LIBTIFF
#ifdef HAVE_LIBJXL
  if (_cfIsJPEGXL(header, sizeof(header)))
    status = _cfImageReadJPEGXL(img, fp, primary, secondary, saturation, hue, 
	    			lut);
  else
#endif // HAVE_LIBJXL
	  
  {
    fclose(fp);
    status = -1;
  }

  if (status)
  {
    cfImageClose(img);
    return (NULL);
  }
  else
    return (img);
}


//
// '_cfImagePutCol()' - Put a column of pixels to an image.
//

int					// O - -1 on error, 0 on success
_cfImagePutCol(
    cf_image_t      *img,		// I - Image
    int             x,			// I - Column
    int             y,			// I - Start row
    int             height,		// I - Column height
    const cf_ib_t   *pixels)		// I - Pixels to put
{
  int		bpp,			// Bytes per pixel
		twidth,			// Width of tile
		count;			// Number of pixels to put
  int		tilex,			// Column within tile
		tiley;			// Row within tile
  cf_ib_t	*ib;			// Pointer to pixels in tile


  if (img == NULL || x < 0 || x >= img->xsize || y >= img->ysize)
    return (-1);

  if (y < 0)
  {
    height += y;
    y = 0;
  }

  if ((y + height) > img->ysize)
    height = img->ysize - y;

  if (height < 1)
    return (-1);

  bpp    = cfImageGetDepth(img);
  twidth = bpp * (CF_TILE_SIZE - 1);
  tilex  = x / CF_TILE_SIZE;
  tiley  = y / CF_TILE_SIZE;

  while (height > 0)
  {
    ib = get_tile(img, x, y);

    if (ib == NULL)
      return (-1);

    img->tiles[tiley][tilex].dirty = 1;
    tiley ++;

    count = CF_TILE_SIZE - (y & (CF_TILE_SIZE - 1));
    if (count > height)
      count = height;

    y      += count;
    height -= count;

    for (; count > 0; count --, ib += twidth)
      switch (bpp)
      {
        case 4 :
            *ib++ = *pixels++;
        case 3 :
            *ib++ = *pixels++;
            *ib++ = *pixels++;
        case 1 :
            *ib++ = *pixels++;
            break;
      }
  }

  return (0);
}


//
// '_cfImagePutRow()' - Put a row of pixels to an image.
//

int					// O - -1 on error, 0 on success
_cfImagePutRow(
    cf_image_t      *img,		// I - Image
    int             x,			// I - Start column
    int             y,			// I - Row
    int             width,		// I - Row width
    const cf_ib_t   *pixels)		// I - Pixel data
{
  int		bpp,			// Bytes per pixel
		count;			// Number of pixels to put
  int		tilex,			// Column within tile
		tiley;			// Row within tile
  cf_ib_t	*ib;			// Pointer to pixels in tile


  if (img == NULL || y < 0 || y >= img->ysize || x >= img->xsize)
    return (-1);

  if (x < 0)
  {
    width += x;
    x = 0;
  }

  if ((x + width) > img->xsize)
    width = img->xsize - x;

  if (width < 1)
    return (-1);

  bpp   = img->colorspace < 0 ? -img->colorspace : img->colorspace;
  tilex = x / CF_TILE_SIZE;
  tiley = y / CF_TILE_SIZE;

  while (width > 0)
  {
    ib = get_tile(img, x, y);

    if (ib == NULL)
      return (-1);

    img->tiles[tiley][tilex].dirty = 1;

    count = CF_TILE_SIZE - (x & (CF_TILE_SIZE - 1));
    if (count > width)
      count = width;
    memcpy(ib, pixels, count * bpp);
    pixels += count * bpp;
    x      += count;
    width  -= count;
    tilex  ++;
  }

  return (0);
}


//
// 'cfImageSetMaxTiles()' - Set the maximum number of tiles to cache.
//
// If the "max_tiles" argument is 0 then the maximum number of tiles is
// computed from the image size or the RIP_CACHE environment variable.
//

void
cfImageSetMaxTiles(
    cf_image_t   *img,			// I - Image to set
    int          max_tiles)		// I - Number of tiles to cache
{
  int	cache_size,			// Size of tile cache in bytes
	min_tiles,			// Minimum number of tiles to cache
	max_size;			// Maximum cache size in bytes
  char	*cache_env,			// Cache size environment variable
	cache_units[255];		// Cache size units


  min_tiles = max(CF_TILE_MINIMUM,
                  1 + max((img->xsize + CF_TILE_SIZE - 1) / CF_TILE_SIZE,
                          (img->ysize + CF_TILE_SIZE - 1) / CF_TILE_SIZE));

  if (max_tiles == 0)
    max_tiles = ((img->xsize + CF_TILE_SIZE - 1) / CF_TILE_SIZE) *
                ((img->ysize + CF_TILE_SIZE - 1) / CF_TILE_SIZE);

  cache_size = max_tiles * CF_TILE_SIZE * CF_TILE_SIZE *
               cfImageGetDepth(img);

  if ((cache_env = getenv("RIP_MAX_CACHE")) != NULL)
  {
    switch (sscanf(cache_env, "%d%254s", &max_size, cache_units))
    {
      case 0 :
          max_size = 32 * 1024 * 1024;
	  break;
      case 1 :
          max_size *= 4 * CF_TILE_SIZE * CF_TILE_SIZE;
	  break;
      case 2 :
          if (tolower(cache_units[0] & 255) == 'g')
	    max_size *= 1024 * 1024 * 1024;
          else if (tolower(cache_units[0] & 255) == 'm')
	    max_size *= 1024 * 1024;
	  else if (tolower(cache_units[0] & 255) == 'k')
	    max_size *= 1024;
	  else if (tolower(cache_units[0] & 255) == 't')
	    max_size *= 4 * CF_TILE_SIZE * CF_TILE_SIZE;
	  break;
    }
  }
  else
    max_size = 32 * 1024 * 1024;

  if (cache_size > max_size)
    max_tiles = max_size / CF_TILE_SIZE / CF_TILE_SIZE /
                cfImageGetDepth(img);

  if (max_tiles < min_tiles)
    max_tiles = min_tiles;

  img->max_ics = max_tiles;

  DEBUG_printf(("max_ics=%d...\n", img->max_ics));
}


//
// 'cfImageCrop()' - Crop an image.
//                   (posw, posh):    Position of left corner
//                   (width, height): Width and height of required image.

cf_image_t*
cfImageCrop(cf_image_t* img,
	    int posw,
	    int posh,
	    int width,
	    int height)
{
  int image_width = cfImageGetWidth(img);
  cf_image_t* temp = calloc(1, sizeof(cf_image_t));
  cf_ib_t *pixels = (cf_ib_t*)malloc(img->xsize * cfImageGetDepth(img));

  temp->cachefile = -1;
  temp->max_ics = CF_TILE_MINIMUM;
  temp->colorspace = img->colorspace;
  temp->xppi = img->xppi;
  temp->yppi = img->yppi;
  temp->num_ics = 0;
  temp->first = temp->last = NULL;
  temp->tiles = NULL;
  temp->xsize = width;
  temp->ysize = height;

  for (int i = posh; i < min(cfImageGetHeight(img), posh + height); i ++)
  {
    cfImageGetRow(img, posw, i, min(width, image_width - posw), pixels);
    _cfImagePutRow(temp, 0, i - posh, min(width, image_width - posw), pixels);
  }

  free(pixels);

  return (temp);
}


//
// 'flush_tile()' - Flush the least-recently-used tile in the cache.
//

static int
flush_tile(cf_image_t *img)		// I - Image
{
  int		bpp;			// Bytes per pixel
  cf_itile_t	*tile;			// Pointer to tile


  bpp = cfImageGetDepth(img);
  if(img == NULL || img->first == NULL || img->first->tile == NULL)
    return (-1);

  tile = img->first->tile;

  if (!tile->dirty)
  {
    tile->ic = NULL;
    return (0);
  }

  if (img->cachefile < 0)
  {
    if ((img->cachefile = cupsCreateTempFd(NULL, NULL, img->cachename,
                                     sizeof(img->cachename))) < 0)
    {
      tile->ic    = NULL;
      tile->dirty = 0;
      return (0);
    }

    DEBUG_printf(("Created swap file \"%s\"...\n", img->cachename));
  }

  if (tile->pos >= 0)
  {
    if (lseek(img->cachefile, tile->pos, SEEK_SET) != tile->pos)
    {
      tile->ic    = NULL;
      tile->dirty = 0;
      return (0);
    }
  }
  else
  {
    if ((tile->pos = lseek(img->cachefile, 0, SEEK_END)) < 0)
    {
      tile->ic    = NULL;
      tile->dirty = 0;
      return (0);
    }
  }

  if (write(img->cachefile, tile->ic->pixels,
	    bpp * CF_TILE_SIZE * CF_TILE_SIZE) == -1)
    DEBUG_printf(("Error writing cache tile!"));

  tile->ic    = NULL;
  tile->dirty = 0;
  return (0);
}


//
// 'get_tile()' - Get a cached tile.
//

static cf_ib_t *			// O - Pointer to tile or NULL
get_tile(cf_image_t *img,		// I - Image
         int          x,		// I - Column in image
         int          y)		// I - Row in image
{
  int		bpp,			// Bytes per pixel
		tilex,			// Column within tile
		tiley,			// Row within tile
		xtiles,			// Number of tiles horizontally
		ytiles;			// Number of tiles vertically
  cf_ic_t	*ic;			// Cache pointer
  cf_itile_t	*tile;			// Tile pointer


  if (img->tiles == NULL)
  {
    xtiles = (img->xsize + CF_TILE_SIZE - 1) / CF_TILE_SIZE;
    ytiles = (img->ysize + CF_TILE_SIZE - 1) / CF_TILE_SIZE;

   /*
    * We check the image validity (f.e. whether xsize and ysize are
    * greater than 0) during opening the file, but it happens several
    * functions before and reader can miss it. Add the check for stressing
    * out such cases are not accepted, which adds readability and fixes
    * false positives of coverity programs.
    */
    if (xtiles <= 0 || ytiles <= 0)
      return (NULL);

    DEBUG_printf(("Creating tile array (%dx%d)\n", xtiles, ytiles));

    if ((img->tiles = calloc(ytiles, sizeof(cf_itile_t *))) == NULL)
      return (NULL);

    if ((tile = calloc(ytiles, xtiles * sizeof(cf_itile_t))) == NULL)
      return (NULL);

    for (tiley = 0; tiley < ytiles; tiley ++)
    {
      img->tiles[tiley] = tile;
      for (tilex = xtiles; tilex > 0; tilex --, tile ++)
        tile->pos = -1;
    }
  }

  bpp   = cfImageGetDepth(img);
  tilex = x / CF_TILE_SIZE;
  tiley = y / CF_TILE_SIZE;
  tile  = img->tiles[tiley] + tilex;
  x     &= (CF_TILE_SIZE - 1);
  y     &= (CF_TILE_SIZE - 1);

  if ((ic = tile->ic) == NULL)
  {
    if (img->num_ics < img->max_ics)
    {
      if ((ic = calloc(1, sizeof(cf_ic_t) +
                       bpp * CF_TILE_SIZE * CF_TILE_SIZE)) == NULL)
      {
        if (img->num_ics == 0)
	  return (NULL);

        flush_tile(img);
	ic = img->first;
      }
      else
      {
	ic->pixels = ((cf_ib_t *)ic) + sizeof(cf_ic_t);

	img->num_ics ++;

	DEBUG_printf(("Allocated cache tile %d (%p)...\n", img->num_ics, ic));
      }
    }
    else
    {
      DEBUG_printf(("Flushing old cache tile (%p)...\n", img->first));

      int res = flush_tile(img);
      if(res)
      {
        return NULL;
      }
      ic = img->first;
    }

    ic->tile = tile;
    tile->ic = ic;

    if (tile->pos >= 0)
    {
      DEBUG_printf(("Loading cache tile from file position " CUPS_LLFMT "...\n",
                    CUPS_LLCAST tile->pos));

      lseek(img->cachefile, tile->pos, SEEK_SET);
      if (read(img->cachefile, ic->pixels,
	       bpp * CF_TILE_SIZE * CF_TILE_SIZE) == -1)
	DEBUG_printf(("Error reading cache tile!"));
    }
    else
    {
      DEBUG_puts("Clearing cache tile...");

      memset(ic->pixels, 0, bpp * CF_TILE_SIZE * CF_TILE_SIZE);
    }
  }

  if (ic == img->first)
  {
    if (ic->next != NULL)
      ic->next->prev = NULL;

    img->first = ic->next;
    ic->next   = NULL;
    ic->prev   = NULL;
  }
  else if (img->first == NULL)
    img->first = ic;

  if (ic != img->last)
  {
    //
    // Remove the cache entry from the list...
    //

    if (ic->prev != NULL)
      ic->prev->next = ic->next;
    if (ic->next != NULL)
      ic->next->prev = ic->prev;

    //
    // And add it to the end...
    //

    if (img->last != NULL)
      img->last->next = ic;

    ic->prev  = img->last;
    img->last = ic;
  }

  ic->next = NULL;

  return (ic->pixels + bpp * (y * CF_TILE_SIZE + x));
}


#ifdef HAVE_EXIF
//
// Helper function required by EXIF read function
//

static void trim_spaces(char *buf)
{
  char *s = buf - 1;
  for (; *buf; ++buf)
  {
    if (*buf != ' ')
      s = buf;
  }
  *++s = 0; // null terminate the string on the first of the final spaces
}


//
// Helper function to extract bytes from image files
//

static unsigned char *
find_bytes(FILE *fp,
	   long int *size)
{
  unsigned char *buf;

  long int originalOffset = ftell(fp);
  fseek(fp, 0L, SEEK_END);

  // calculating the size of the file
  long int res = ftell(fp);

  buf = (unsigned char *)malloc(res * sizeof(unsigned char) + 1);
  fseek(fp, 0, SEEK_SET);

  if (fread(buf, 1, res, fp) < res)
  {
    free(buf);
    buf = NULL;
    *size = 0;
  }
  else
    *size = res + 1;

  fseek(fp, originalOffset, SEEK_SET);

  return (buf);
}


//
// Implementation for EXIF read function
//

int
_cfImageReadEXIF(cf_image_t *img,
		 FILE *fp)
{

  if (fp == NULL)
  {
    return -1;
  }

  long int bufSize = 0;

  unsigned char *buf = find_bytes(fp, &bufSize);

  ExifData *ed = NULL;

  if (buf == NULL || bufSize <= 0 ||
      (ed = exif_data_new_from_data(buf, bufSize)) == NULL)
  {
    if (buf)
      free(buf);
    DEBUG_printf(("DEBUG: No EXIF data found"));
    return (2);
  }

  ExifIfd ifd = EXIF_IFD_0;
  ExifTag tagX = EXIF_TAG_X_RESOLUTION;
  ExifTag tagY = EXIF_TAG_Y_RESOLUTION;

  ExifEntry *entryX = exif_content_get_entry(ed->ifd[ifd], tagX);

  ExifEntry *entryY = exif_content_get_entry(ed->ifd[ifd], tagY);

  if (entryX == NULL || entryY == NULL)
  {
    if (buf)
      free(buf);
    DEBUG_printf(("DEBUG: No EXIF data found"));
    return (2);
  }

  if (entryX)
  {
    char buf1[1024];

    exif_entry_get_value(entryX, buf1, sizeof(buf1));

    trim_spaces(buf1);

    if (*buf1)
    {
      int xRes;
      sscanf(buf1, "%d", &xRes);
      img->xppi = xRes;
    }
    else
    {
      free(buf);
      return (2);
    }
  }

  if (entryY)
  {
    char buf2[1024];

    exif_entry_get_value(entryY, buf2, sizeof(buf2));
    trim_spaces(buf2);

    if (*buf2)
    {
      int yRes;
      sscanf(buf2, "%d", &yRes);
      img->yppi = yRes;
    }
    else{
      free(buf);
      return (2);
    }
  }

  free(buf);
  return (1);
}
#endif // HAVE_EXIF
