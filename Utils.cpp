// Utils.cpp, image processing utilities v1.00, Andrew MacKinnon andrewmackinnon@rogers.com
// See MIT_License.txt

#include <ctype.h>
#include "Utils.h"

//TODO: Refactor into C++ classes

/******************************************************************************
* Static variables
*****************************************************************************/
// RGB to YUV coefficients
// 8-bit R'G'B' computer (0-255) to Y'CbCr Rec.601 integer standard
static const double RGBtoYUV601[3][4] =
{
	{ 65.738, 129.057, 25.064, 16 },
	{ -37.946, -74.494, 112.439, 128 },
	{ 112.439, -94.154, -18.285, 128 }
};

// YUV to RGB coefficients
// Y'CbCr Rec.601 to 8-bit R'G'B' computer (0-255) 
static const double YUV601toRGB[3][4] =
{
	{ 298.082, 0, 408.583, -16 },
	{ 298.082, -100.291, -208.120, -128 },
	{ 298.082, 516.411, 0, -128 }
};

// 8-bit R'G'B' computer (0-255) to Y'CbCr Rec.709 integer standard
static const double RGBtoYUV709[3][4] =
{
	{ 46.742, 157.243, 15.874, 16 },
	{ -25.765, -86.674, 112.439, 128 },
	{ 112.439, -102.129, -10.310, 128 }
};

// YUV to RGB coefficients
// Y'CbCr Rec.709 to 8-bit R'G'B' computer (0-255) 
static const double YUV709toRGB[3][4] =
{
	{ 298.082, 0, 458.942, -16 },
	{ 298.082, -54.592, -136.425, -128 },
	{ 298.082, 540.775, 0, -128 }
};

// Limits for YUV
static const int YUV_YMIN = 16;
static const int YUV_YMAX = 235;
static const int YUV_UVMIN = 16;
static const int YUV_UVMAX = 240;

// Bitmap header. Uses common BITMAPINFOHEADER header
//#ifdef _WIN32
#ifdef _MSC_VER		// MS Visual Studio compiler
#pragma pack(push)
#pragma pack(1)
typedef struct
{
	// Bitmap file header
	unsigned short  fileType;		// 2 bytes
	unsigned int    fileSize;		// 4 bytes
	unsigned int    reserved1;		// 2 reserved fields, 2 bytes each
	unsigned int    dataOffset;		// 4 bytes, start address of pixel data

	// DIB header (bitmap information header)
	unsigned int    headerSize;		// 4 bytes, the size of this header (40 bytes)
	signed int      bitmapWidth;	// 4 bytes, signed int
	signed int      bitmapHeight;	// 4 bytes, signed int
	unsigned short  numPlanes;		// 2 bytes, must equal 1
	unsigned short  colorDepth;		// 2 bytes, only 24bpp supported
	unsigned int    reserved2;		// 4 bytes, compression not supported
	unsigned int    bitmapSize;		// 4 bytes, size of the raw bitmap data
	unsigned int    reserved3[4];	// H/V resolution, palettes not supported
} BitmapFileHeader;
#pragma pack(pop)
#else //GCC
typedef struct __attribute__((__packed__))
{
	// Bitmap file header
	unsigned short  fileType;
	unsigned int    fileSize;
	unsigned int    reserved1;
	unsigned int    dataOffset;

	// DIB header (bitmap information header)
	unsigned int    headerSize;
	signed int      bitmapWidth;
	signed int      bitmapHeight;
	unsigned short  numPlanes;
	unsigned short  colorDepth;
	unsigned int    reserved2;		// compression not supported
	unsigned int    bitmapSize;
	unsigned int    reserved3[4];	// H/V resolution, palettes not supported
} BitmapFileHeader;
#endif //UNIX

/******************************************************************************
* PRIVATE FUNCTIONS forward declarations
*****************************************************************************/
// Converts 8BPP RGB image to 8BPP YUV444/422/420
static bool RGBImage2YUV(const IMAGE *pImageIn, IMAGE *pImageOut);

// Converts 8BPP YUV444/422/420 image to 8BPP RGB
static bool YUVImage2RGB(const IMAGE *pImageIn, IMAGE *pImageOut);

// Converts 8BPP YUV pixel to 8BPP RGB
static void YUVPixel2RGB(PIXEL yuvPixel[], PIXEL rgbPixel[]);

// Converts 8BPP RGB pixel to 8BPP YUV Rec.601
// Note: Clamps only to 0..PIXMAX boundary, not 16..235/16..240 range 
// to preserve excursions for indermediate processing stages
static void RGBPixel2YUV(PIXEL rgbPixel[], PIXEL yuvPixel[]);

/******************************************************************************
* PRIVATE FUNCTIONS
*****************************************************************************/
// Allocate memory for a 2D array
// x=width, y=height
void **Alloc2DArray(int typeSize, int y, int x)
{
	void **pY = (void **)malloc(y*sizeof(void *));
	void *array2D = (void *)calloc(x*y, typeSize);
	unsigned char *pCurr = (unsigned char *)array2D;
	int yi;

	if (!pY || !array2D)
		return NULL;

	for (yi = 0; yi < y; yi++)
	{
		*(pY + yi) = pCurr;
		pCurr += x*typeSize;
	}
	return pY;
}

void Free2DArray(void **array2D)
{
	if (*array2D)
		free(*array2D);

	if (array2D)
		free(array2D);

	return;
}

// Allocate memory for a 3D array
// x=width, y=height, z=depth
void ***Alloc3DArray(int typeSize, int z, int y, int x)
{
	void ***pZ = (void ***)malloc(z*sizeof(void *));
	void **pY = (void **)malloc(y*z*sizeof(void *));
	void *array3D = (void *)calloc(x*y*z, typeSize);
	unsigned char *pCurr = (unsigned char *)array3D;
	int yi, zi;

	if (!pZ || !pY || !array3D)
		return NULL;

	for (zi = 0; zi < z; zi++)
	{
		*(pZ + zi) = pY + zi*y;
		for (yi = 0; yi < y; yi++)
		{
			*(pY + yi + zi*y) = pCurr;
			pCurr += x*typeSize;
		}
	}
	return pZ;
}

void Free3DArray(void ***array3D)
{
	if (**array3D)
		free(**array3D);

	if (*array3D)
		free(*array3D);

	if (array3D)
		free(array3D);

	return;
}

// Converts 8BPP YUV Rec.601 pixel to 8BPP RGB
static void YUVPixel2RGB(PIXEL yuvPixel[], PIXEL rgbPixel[])
{
	// Account for YUV offsets before matrix multiply
	double tempPixel[3] = {
		(double)yuvPixel[Y_PLANE] + YUV601toRGB[Y_PLANE][3],
		(double)yuvPixel[U_PLANE] + YUV601toRGB[U_PLANE][3],
		(double)yuvPixel[V_PLANE] + YUV601toRGB[V_PLANE][3] };

	rgbPixel[R_PLANE] = (PIXEL)(CLAMP((YUV601toRGB[0][0] * tempPixel[Y_PLANE] +
		YUV601toRGB[0][1] * tempPixel[U_PLANE] +
		YUV601toRGB[0][2] * tempPixel[V_PLANE]) / 256.0 + 0.5, 0, PIXMAX));

	rgbPixel[G_PLANE] = (PIXEL)(CLAMP((YUV601toRGB[1][0] * tempPixel[Y_PLANE] +
		YUV601toRGB[1][1] * tempPixel[U_PLANE] +
		YUV601toRGB[1][2] * tempPixel[V_PLANE]) / 256.0 + 0.5, 0, PIXMAX));

	rgbPixel[B_PLANE] = (PIXEL)(CLAMP((YUV601toRGB[2][0] * tempPixel[Y_PLANE] +
		YUV601toRGB[2][1] * tempPixel[U_PLANE] +
		YUV601toRGB[2][2] * tempPixel[V_PLANE]) / 256.0 + 0.5, 0, PIXMAX));
}

// Converts 8BPP RGB pixel to 8BPP YUV Rec.601
// Note: Clamps only to 0..PIXMAX boundary, not 16..235/16..240 range 
// to preserve excursions for indermediate processing stages
static void RGBPixel2YUV(PIXEL rgbPixel[], PIXEL yuvPixel[])
{
	yuvPixel[Y_PLANE] = (PIXEL)(CLAMP((RGBtoYUV601[0][0] * rgbPixel[R_PLANE] +
		RGBtoYUV601[0][1] * rgbPixel[G_PLANE] +
		RGBtoYUV601[0][2] * rgbPixel[B_PLANE]) / 256.0 + RGBtoYUV601[0][3] + 0.5, 0, PIXMAX));

	yuvPixel[U_PLANE] = (PIXEL)(CLAMP((RGBtoYUV601[1][0] * rgbPixel[R_PLANE] +
		RGBtoYUV601[1][1] * rgbPixel[G_PLANE] +
		RGBtoYUV601[1][2] * rgbPixel[B_PLANE]) / 256.0 + RGBtoYUV601[1][3] + 0.5, 0, PIXMAX));

	yuvPixel[V_PLANE] = (PIXEL)(CLAMP((RGBtoYUV601[2][0] * rgbPixel[R_PLANE] +
		RGBtoYUV601[2][1] * rgbPixel[G_PLANE] +
		RGBtoYUV601[2][2] * rgbPixel[B_PLANE]) / 256.0 + RGBtoYUV601[2][3] + 0.5, 0, PIXMAX));
}

// Converts 8BPP YUV444/422/420 image to 8BPP RGB
static bool YUVImage2RGB(const IMAGE *pImageIn, IMAGE *pImageOut)
{
	PIXEL yuvPixel[3];
	PIXEL rgbPixel[3];

	// Output parameters should already have been set
	//pImageOut->colorSpace = RGB;
	//pImageOut->height = pImageIn->height;
	//pImageOut->width = pImageIn->width;

	// Verify correct formats
	if ((pImageIn->colorSpace != YUV444) && (pImageIn->colorSpace != YUV422) &&
		(pImageIn->colorSpace != YUV420))
	{
		fprintf(stderr, "ERROR UTILS::YUVImage2RGB(): Incorrect input color space!\n");
		return FALSE;
	}

	// Verify 8BPP
	if (pImageIn->precision != BPP8 || pImageOut->precision != BPP8)
	{
		fprintf(stderr, "ERROR UTILS::RGBImage2YUV(): Only 8BPP precision supported!\n");
		return FALSE;
	}

	switch (pImageIn->colorSpace)
	{
	case YUV444:
		for (int y = 0; y < pImageOut->height; y++){
			for (int x = 0; x < pImageOut->width; x++) {
				GetPixel(pImageIn, y, x, REPEAT, yuvPixel);
				YUVPixel2RGB(yuvPixel, rgbPixel);
				SetPixel(pImageOut, y, x, rgbPixel);
			}
		}
		break;
	case YUV422:
		for (int y = 0; y < pImageOut->height; y++){
			for (int x = 0; x < pImageOut->width; x += 2) {

				// Convert cosited pixel
				GetPixel(pImageIn, y, x, REPEAT, yuvPixel);
				YUVPixel2RGB(yuvPixel, rgbPixel);
				SetPixel(pImageOut, y, x, rgbPixel);

				// Convert non-cosited pixel
				GetPixel(pImageIn, y, x + 1, REPEAT, yuvPixel);
				YUVPixel2RGB(yuvPixel, rgbPixel);
				SetPixel(pImageOut, y, x + 1, rgbPixel);
			}
		}
		break;
	case YUV420:
		for (int y = 0; y < pImageOut->height; y += 2){
			for (int x = 0; x < pImageOut->width; x += 2) {

				// Convert cosited pixel
				GetPixel(pImageIn, y, x, REPEAT, yuvPixel);
				YUVPixel2RGB(yuvPixel, rgbPixel);
				SetPixel(pImageOut, y, x, rgbPixel);

				// Convert non-cosited pixel upper right
				GetPixel(pImageIn, y, x + 1, REPEAT, yuvPixel);
				YUVPixel2RGB(yuvPixel, rgbPixel);
				SetPixel(pImageOut, y, x + 1, rgbPixel);

				// Convert non-cosited pixel lower left
				GetPixel(pImageIn, y + 1, x, REPEAT, yuvPixel);
				YUVPixel2RGB(yuvPixel, rgbPixel);
				SetPixel(pImageOut, y + 1, x, rgbPixel);

				// Convert non-cosited pixel lower right
				GetPixel(pImageIn, y + 1, x + 1, REPEAT, yuvPixel);
				YUVPixel2RGB(yuvPixel, rgbPixel);
				SetPixel(pImageOut, y + 1, x + 1, rgbPixel);
			}
		}
		break;
	default:
		fprintf(stderr, "ERROR UTILS::YUVImage2RGB(): Input image must be in YUV 444/422/420!\n");
		return FALSE;
	}
	return TRUE;
}

// Converts 8BPP RGB image to 8BPP YUV444/422/420
static bool RGBImage2YUV(const IMAGE *pImageIn, IMAGE *pImageOut)
{
	// Output parameters should already have been set
	//pImageOut->height = pImageIn->height;
	//pImageOut->width = pImageIn->width;

	// Verify correct formats
	if ((pImageOut->colorSpace != YUV444) && (pImageOut->colorSpace != YUV422) &&
		(pImageOut->colorSpace != YUV420))
	{
		fprintf(stderr, "ERROR UTILS::RGBImage2YUV(): Incorrect output color space!\n");
		return FALSE;
	}

	// Verify 8BPP
	if (pImageIn->precision != BPP8 || pImageOut->precision != BPP8)
	{
		fprintf(stderr, "ERROR UTILS::RGBImage2YUV(): Only 8BPP precision supported!\n");
		return FALSE;
	}

	PIXEL yuvPixel[3];
	PIXEL rgbPixel[3];

	if (pImageOut->colorSpace == YUV444)
	{
		for (int y = 0; y < pImageOut->height; y++) {
			for (int x = 0; x < pImageOut->width; x++) {
				GetPixel(pImageIn, y, x, REPEAT, rgbPixel);
				RGBPixel2YUV(rgbPixel, yuvPixel);
				SetPixel(pImageOut, y, x, yuvPixel);
			}
		}
	}
	else
	{
		// Convert RGB to YUV444
		IMAGE tempImage = CreateImage(YUV444, pImageOut->width, pImageOut->height);

		for (int y = 0; y < pImageOut->height; y++) {
			for (int x = 0; x < pImageOut->width; x++) {
				GetPixel(pImageIn, y, x, REPEAT, rgbPixel);
				RGBPixel2YUV(rgbPixel, yuvPixel);
				SetPixel(&tempImage, y, x, yuvPixel);
			}
		}

		// Downsample for YUV 422/420
		switch (pImageOut->colorSpace)
		{
		case YUV422:
			for (int y = 0; y < pImageOut->height; y++) {
				for (int x = 0; x < pImageOut->width; x += 2) {

					yuvPixel[U_PLANE] = (GetSubPixel(&tempImage, y, x - 1, REPEAT, U_PLANE) +
						2 * GetSubPixel(&tempImage, y, x, REPEAT, U_PLANE) +
						GetSubPixel(&tempImage, y, x + 1, REPEAT, U_PLANE) + 2) / 4;
					yuvPixel[V_PLANE] = (GetSubPixel(&tempImage, y, x - 1, REPEAT, V_PLANE) +
						2 * GetSubPixel(&tempImage, y, x, REPEAT, V_PLANE) +
						GetSubPixel(&tempImage, y, x + 1, REPEAT, V_PLANE) + 2) / 4;

					// get Y pixel
					yuvPixel[Y_PLANE] = GetSubPixel(&tempImage, y, x, REPEAT, Y_PLANE);

					// Set cosited pixel
					SetPixel(pImageOut, y, x, yuvPixel);

					// Get and set non-cosited Y subpixel
					yuvPixel[Y_PLANE] = GetSubPixel(&tempImage, y, x + 1, REPEAT, Y_PLANE);
					SetSubPixel(pImageOut, y, x + 1, Y_PLANE, yuvPixel[Y_PLANE]);
				}
			}
			break;
		case YUV420:
			for (int y = 0; y < pImageOut->height; y += 2) {
				for (int x = 0; x < pImageOut->width; x += 2) {

					yuvPixel[U_PLANE] = (GetSubPixel(&tempImage, y, x, REPEAT, U_PLANE) +
						GetSubPixel(&tempImage, y, x + 1, REPEAT, U_PLANE) +
						GetSubPixel(&tempImage, y + 1, x, REPEAT, U_PLANE) +
						GetSubPixel(&tempImage, y + 1, x + 1, REPEAT, U_PLANE) + 2) / 4;
					yuvPixel[V_PLANE] = (GetSubPixel(&tempImage, y, x, REPEAT, V_PLANE) +
						GetSubPixel(&tempImage, y, x + 1, REPEAT, V_PLANE) +
						GetSubPixel(&tempImage, y + 1, x, REPEAT, V_PLANE) +
						GetSubPixel(&tempImage, y + 1, x + 1, REPEAT, V_PLANE) + 2) / 4;

					// get Y pixel
					yuvPixel[Y_PLANE] = GetSubPixel(&tempImage, y, x, REPEAT, Y_PLANE);

					// Set cosited pixel
					SetPixel(pImageOut, y, x, yuvPixel);

					// Get and set non-cosited Y subpixels
					yuvPixel[Y_PLANE] = GetSubPixel(&tempImage, y, x + 1, REPEAT, Y_PLANE);
					SetSubPixel(pImageOut, y, x + 1, Y_PLANE, yuvPixel[Y_PLANE]);

					yuvPixel[Y_PLANE] = GetSubPixel(&tempImage, y + 1, x, REPEAT, Y_PLANE);
					SetSubPixel(pImageOut, y + 1, x, Y_PLANE, yuvPixel[Y_PLANE]);

					yuvPixel[Y_PLANE] = GetSubPixel(&tempImage, y + 1, x + 1, REPEAT, Y_PLANE);
					SetSubPixel(pImageOut, y + 1, x + 1, Y_PLANE, yuvPixel[Y_PLANE]);
				}
			}
			break;
		default:
			break;
		}

		// Deallocate memory
		DestroyImage(&tempImage);
	}
	return TRUE;
}



/******************************************************************************
* PUBLIC FUNCTIONS
*****************************************************************************/
// ---------------------------
// Image manipulation routines
// ---------------------------
// Create image of 8BPP pixels
IMAGE CreateImage(ColorSpaces colorSpace, int width, int height)
{
	return CreateImage(colorSpace, width, height, BPP8);
}

// Creates and initializes image structure and returns pointer to new image
// Creates 3D array. This is space inefficient for YUV422/YUV420 types, but allows
// support for YUV444/RGB using uniform 3D array addressing, instead of treating
// first plane differently than second and third plane
// The pixel array's type is determined by the precision parameter to allow support for both
// fixed precision (8BPP) and float(double) precision pixels.
IMAGE CreateImage(ColorSpaces colorSpace, int width, int height, PixelPrecision precision)
{
	IMAGE newImage;

	if (precision == BPP8)
	{
		newImage.pixArray = Create3DArray(PIXEL, 3, height, width);
		if (newImage.pixArray == NULL)
		{
			fprintf(stderr, "ERROR UTILS::CreateImage(): Could not allocate image memory\n");
			exit(FALSE);
		}
		newImage.dblPixArray = NULL;
	}
	else if (precision == DOUBLE)
	{
		newImage.dblPixArray = Create3DArray(double, 3, height, width);
		if (newImage.dblPixArray == NULL)
		{
			fprintf(stderr, "ERROR UTILS::CreateImage(): Could not allocate image memory\n");
			exit(FALSE);
		}
		newImage.pixArray = NULL;
	}
	else
	{
		fprintf(stderr, "ERROR UTILS::CreateImage(): Unsupported pixel precision!\n");
		exit(FALSE);
	}

	newImage.colorSpace = colorSpace;
	newImage.height = height;
	newImage.width = width;
	newImage.precision = precision;

	return(newImage);
}

// Destroys image previously created using CreateImage();
void DestroyImage(IMAGE *pImage)
{
	if (pImage->pixArray)
		Destroy3DArray(pImage->pixArray);
	if (pImage->dblPixArray)
		Destroy3DArray(pImage->dblPixArray);
}

// Copies a given image
bool CopyImage(const IMAGE *pImageIn, IMAGE *pImageOut)
{
	if ((pImageIn->width != pImageOut->width) || (pImageIn->height != pImageOut->height))
	{
		fprintf(stderr, "ERROR: UTILS::CopyImage(): Images have different dimensions!\n");
		return FALSE;
	}
	if (!((pImageIn->pixArray && pImageOut->pixArray) || (pImageIn->dblPixArray && pImageOut->dblPixArray)))
	{
		fprintf(stderr, "ERROR: UTILS::CopyImage(): Image precisions not the same or image memory unallocated!\n");
		return FALSE;
	}

	// Copy pixels
	unsigned int size;
	if (pImageIn->pixArray)
	{
		size = pImageIn->width * pImageIn->height * sizeof(PIXEL)* 3;
		memcpy(&(pImageOut->pixArray[0][0][0]), &(pImageIn->pixArray[0][0][0]), size);
	}
	else if (pImageIn->dblPixArray)
	{
		size = pImageIn->width * pImageIn->height * sizeof(double)* 3;
		memcpy(&(pImageOut->dblPixArray[0][0][0]), &(pImageIn->dblPixArray[0][0][0]), size);
	}
	else
	{
		fprintf(stderr, "ERROR: UTILS::CopyImage(): Unsupported pixel precision!\n");
		return FALSE;
	}

	// Copy colorspace info
	pImageOut->colorSpace = pImageIn->colorSpace;
	pImageOut->precision = pImageIn->precision;

	return TRUE;
}

// Takes gamma-corrected pImageIn, applies supplied fwdGamma table to convert to linear light pImageOut
// Y'UV in YUV out, or R'G'B' in RGB out
bool DegammaImage(const IMAGE *pImageIn, IMAGE *pImageOut, double fwdGamma[])
{
	if ((pImageIn->width != pImageOut->width) || (pImageIn->height != pImageOut->height))
	{
		fprintf(stderr, "ERROR UTILS::DegammaImage(): Images have different dimensions!\n");
		return FALSE;
	}
	if (!pImageIn->pixArray)
	{
		fprintf(stderr, "ERROR UTILS::DegammaImage(): Input image array must be 8 bit precision!\n");
		return FALSE;
	}
	if (!pImageOut->dblPixArray)
	{
		fprintf(stderr, "ERROR UTILS::DegammaImage(): Output image array must be double precision!\n");
		return FALSE;
	}
	if (pImageIn->colorSpace != pImageOut->colorSpace)
	{
		fprintf(stderr, "ERROR UTILS::DegammaImage(): Images have different colorspaces!\n");
		return FALSE;
	}

	// Gamma convert all planes if they are RGB, otherwise gamma convert Y and simply divide down UV
	if (pImageIn->colorSpace == RGB)
	{
		for (int plane = R_PLANE; plane <= B_PLANE; plane++)
		{

			for (int y = 0; y < pImageIn->height; y++)
			{
				for (int x = 0; x < pImageIn->width; x++)
				{
					int pixval = (int)(CLAMP(pImageIn->pixArray[plane][y][x], 0, FWD_GAMMA_LUTSIZE - 1));
					pImageOut->dblPixArray[plane][y][x] = fwdGamma[pixval];
				}
			}
		}
	}
	else
	{
		for (int y = 0; y < pImageIn->height; y++)
		{
			for (int x = 0; x < pImageIn->width; x++)
			{
				int pixval = (int)(CLAMP(pImageIn->pixArray[Y_PLANE][y][x], 0, FWD_GAMMA_LUTSIZE - 1));
				pImageOut->dblPixArray[Y_PLANE][y][x] = fwdGamma[pixval];
			}
		}
		for (int plane = U_PLANE; plane <= V_PLANE; plane++)
		{
			for (int y = 0; y < pImageIn->height; y++)
			{
				for (int x = 0; x < pImageIn->width; x++)
				{
					int pixval = (int)(CLAMP(pImageIn->pixArray[plane][y][x], 0, FWD_GAMMA_LUTSIZE - 1));
					pImageOut->dblPixArray[plane][y][x] = (double)pixval / (FWD_GAMMA_LUTSIZE - 1);
				}
			}
		}
	}
	return TRUE;
}

// Takes gamma-corrected pImageIn, applies supplied fwdGamma table to convert to linear light pImageOut
// YUV in Y'UV out, or RGB in R'G'B' out
bool GammaImage(const IMAGE *pImageIn, IMAGE *pImageOut, PIXEL bwdGamma[])
{
	if ((pImageIn->width != pImageOut->width) || (pImageIn->height != pImageOut->height))
	{
		fprintf(stderr, "ERROR UTILS::GammaImage(): Images have different dimensions!\n");
		return FALSE;
	}
	if (!pImageIn->dblPixArray)
	{
		fprintf(stderr, "ERROR UTILS::GammaImage(): Input image array must be 8 bit precision!\n");
		return FALSE;
	}
	if (!pImageOut->pixArray)
	{
		fprintf(stderr, "ERROR UTILS::GammaImage(): Output image array must be double precision!\n");
		return FALSE;
	}
	if (pImageIn->colorSpace != pImageOut->colorSpace)
	{
		fprintf(stderr, "ERROR UTILS::GammaImage(): Images have different colorspaces!\n");
		return FALSE;
	}

	// Gamma convert all planes if they are RGB, otherwise gamma convert Y and simply multiply up UV
	if (pImageIn->colorSpace == RGB)
	{
		for (int plane = R_PLANE; plane <= B_PLANE; plane++)
		{

			for (int y = 0; y < pImageIn->height; y++)
			{
				for (int x = 0; x < pImageIn->width; x++)
				{
					int pixval = (int)
						(CLAMP(pImageIn->dblPixArray[plane][y][x] * (BWD_GAMMA_LUTSIZE - 1) + 0.5f, 0, BWD_GAMMA_LUTSIZE - 1));
					pImageOut->pixArray[plane][y][x] = bwdGamma[pixval];
				}
			}
		}
	}
	else
	{
		for (int y = 0; y < pImageIn->height; y++)
		{
			for (int x = 0; x < pImageIn->width; x++)
			{
				int pixval = (int)
					(CLAMP(pImageIn->dblPixArray[Y_PLANE][y][x] * (BWD_GAMMA_LUTSIZE - 1) + 0.5f, 0, BWD_GAMMA_LUTSIZE - 1));
				pImageOut->pixArray[Y_PLANE][y][x] = bwdGamma[pixval];
			}
		}
		for (int plane = U_PLANE; plane <= V_PLANE; plane++)
		{
			for (int y = 0; y < pImageIn->height; y++)
			{
				for (int x = 0; x < pImageIn->width; x++)
				{
					PIXEL pixval = (PIXEL)(CLAMP(pImageIn->dblPixArray[plane][y][x] * 
						(FWD_GAMMA_LUTSIZE - 1) + 0.5f, 0, (FWD_GAMMA_LUTSIZE - 1)));
					pImageOut->pixArray[plane][y][x] = pixval;
				}
			}
		}
	}
	return TRUE;
}


// Color space conversion
bool ConvertImage(const IMAGE *pImageIn, IMAGE *pImageOut)
{
	if ((pImageIn->width != pImageOut->width) || (pImageIn->height != pImageOut->height))
	{
		fprintf(stderr, "ERROR UTILS::ConvertImage(): Images have different dimensions!\n");
		return FALSE;
	}

	if (pImageIn->colorSpace == RGB && 
		(pImageOut->colorSpace == YUV422 ||
		pImageOut->colorSpace == YUV444 ||
		pImageOut->colorSpace == YUV420))
	{
		if (!RGBImage2YUV(pImageIn, pImageOut))
			return FALSE;
	}
	else if ((pImageIn->colorSpace == YUV422 ||
		pImageIn->colorSpace == YUV444 ||
		pImageIn->colorSpace == YUV420) &&
		pImageOut->colorSpace == RGB)
	{
		if (!YUVImage2RGB(pImageIn, pImageOut))
			return FALSE;
	}
	else if (pImageIn->colorSpace == pImageOut->colorSpace)
	{
		if (!CopyImage(pImageIn, pImageOut))
			return FALSE;
	}
	else
	{
		fprintf(stderr, "ERROR UTILS::ConvertImage(): Unsupported input/output format combination!\n");
		return FALSE;
	}
	return TRUE;
}

// Adjust input address i based on edgeMethod. 
// 1D, either H/V depending on whether max H/D supplied as imageDimMax
int HandleEdgeCase(int i, int imageDimMax, EdgeMethod edgeMethod)
{
	int xy = i;

	switch (edgeMethod)
	{
	case MIRROR:
		if (xy < 0)
			xy = -xy;
		if (xy >= imageDimMax)
			xy = imageDimMax * 2 - xy - 2;
		break;

	case REPEAT:
	default:
		//xy = CLAMP(xy, 0, pImageIn->width - 1);
		break;
	}
	// Always do clamp to image boundaries in case edgeMethod==MIRROR and mirrored
	// address is still outside image
	xy = CLAMP(xy, 0, imageDimMax - 1);

	return(xy);
}

// Divide down x,y addresses for plane[1] and plane [2]
void HandleColorspaceAddress(int *x, int *y, ColorSpaces colorSpace)
{
	switch (colorSpace)
	{
	case YUV422:
		*x /= 2;
		break;
	case YUV420:
		*x /= 2;
		*y /= 2;
		break;
	case RGB:
	case YUV444:
	default:
		break;
	}
}

// Gets subpixel (R, G, B, Y, U, or V)
// x, y co-ordinates are internally divided down for YUV422/YUV420 UV planes
PIXEL GetSubPixel(const IMAGE *pImage, int y, int x, const EdgeMethod edgeMethod, const int plane)
{
	x = HandleEdgeCase(x, pImage->width, edgeMethod);
	y = HandleEdgeCase(y, pImage->height, edgeMethod);

	if (plane == U_PLANE || plane == V_PLANE)
		HandleColorspaceAddress(&x, &y, pImage->colorSpace);

	return(pImage->pixArray[plane][y][x]);
}

// Gets subpixel (R, G, B, Y, U, or V)
// x, y co-ordinates are internally divided down for YUV422/YUV420 UV planes
// NOTE: This function has no edge handling and does not check the supplied pixel address
//       against the iamge boundaries. Do not use if there is any chance of going outside!
PIXEL GetSubPixel(const IMAGE *pImage, int y, int x, const int plane)
{
	if (plane == U_PLANE || plane == V_PLANE)
		HandleColorspaceAddress(&x, &y, pImage->colorSpace);

	return(pImage->pixArray[plane][y][x]);
}

// Sets subpixel (R, G, B, Y, U, or V)
// x, y co-ordinates are NOT internally divided down for YUV422/YUV420 UV planes
void SetSubPixel(const IMAGE *pImage, int y, int x, const int plane, PIXEL pixVal)
{
	// Check location is within image dimensions. If not, fail silently to avoid error log clutter
	if (!((y >= 0) && (x >= 0) && (y < pImage->height) && (x < pImage->width)))
		return;

	if (plane == U_PLANE || plane == V_PLANE)
		HandleColorspaceAddress(&x, &y, pImage->colorSpace);

	pImage->pixArray[plane][y][x] = pixVal;
}

// Gets YUV or RGB pixels from an image
// x, y co-ordinates refer to Y/R plane and are internally divided down for YUV422/YUV420 UV planes
bool GetPixel(const IMAGE *pImage, int y, int x, const EdgeMethod edgeMethod, PIXEL pixel[])
{
	if (!pImage->pixArray)
	{
		pixel[Y_PLANE] = pixel[U_PLANE] = pixel[V_PLANE] = 0;
		return FALSE;
	}

	x = HandleEdgeCase(x, pImage->width, edgeMethod);
	y = HandleEdgeCase(y, pImage->height, edgeMethod);

	pixel[Y_PLANE] = pImage->pixArray[Y_PLANE][y][x];

	HandleColorspaceAddress(&x, &y, pImage->colorSpace);

	pixel[U_PLANE] = pImage->pixArray[U_PLANE][y][x];
	pixel[V_PLANE] = pImage->pixArray[V_PLANE][y][x];

	return TRUE;
}

// Gets YUV or RGB pixels from an image
// x, y co-ordinates refer to Y/R plane and are internally divided down for YUV422/YUV420 UV planes
bool GetPixel(const IMAGE *pImage, int y, int x, const EdgeMethod edgeMethod, double pixel[])
{
	if (!pImage->dblPixArray)
	{
		pixel[Y_PLANE] = pixel[U_PLANE] = pixel[V_PLANE] = 0;
		return FALSE;
	}

	x = HandleEdgeCase(x, pImage->width, edgeMethod);
	y = HandleEdgeCase(y, pImage->height, edgeMethod);

	pixel[Y_PLANE] = pImage->dblPixArray[Y_PLANE][y][x];

	HandleColorspaceAddress(&x, &y, pImage->colorSpace);

	pixel[U_PLANE] = pImage->dblPixArray[U_PLANE][y][x];
	pixel[V_PLANE] = pImage->dblPixArray[V_PLANE][y][x];

	return TRUE;
}

// Sets YUV or RGB pixel from image
// x, y co-ordinates refer to Y/R plane and are internally divided down for YUV422/YUV420 UV planes
void SetPixel(const IMAGE *pImage, int y, int x, PIXEL pixel[])
{
	// Check location is within image dimensions. If not, fail silently to avoid error log clutter
	if (!((y >= 0) && (x >= 0) && (y < pImage->height) && (x < pImage->width)))
		return;

	// Set R/Y pixel
	pImage->pixArray[Y_PLANE][y][x] = pixel[Y_PLANE];

	HandleColorspaceAddress(&x, &y, pImage->colorSpace);

	pImage->pixArray[U_PLANE][y][x] = pixel[U_PLANE];
	pImage->pixArray[V_PLANE][y][x] = pixel[V_PLANE];
}


// ---------------------------
// General image file I/O
// ---------------------------

// Detects if file exists
bool FileExists(const char *fileName)
{
	FILE *file;

	if ((file = fopen(fileName, "rb")) != NULL)
	{
		fclose(file);
		return TRUE;
	}
	return FALSE;
}

// Determine file type by file extension.
bool DetectFileType(const char *fileName, FileType* fileType)
{
	const char *pChar;
	char extension[5];

	*fileType = UNSUPPORTED_FILE;

	pChar = strrchr(fileName, '.'); // find last '.' in string; letters afterwards should be extension
	if (pChar == NULL)
	{
		return FALSE;
	}
	else
	{
		strncpy(extension, (pChar + 1), 4); // Leave room for 4-char extension, for future additional type support
		if (!strncmp(extension, "yuv", 3))
			*fileType = YUV_FILE;
		else if (!strncmp(extension, "bmp", 3))
			*fileType = BMP_FILE;
	}
	return TRUE;
}

// Determine how many frames in YUV file or BMP sequence
//
// TODO: Add support for multiframe BMP inputs via file numbers:
//    <path>/<basename>XXXXX.bmp where XXXXX are digits
//    numFrames is highest number that can be reached via incrementing from 00000
// If supplied filename is of form <path>/<basename>.bmp where <basename> does not 
// end in a digit, only a single frame is implied
//
// Supports only single frame BMP inputs
// Supports multiframe for raw YUV files by numSubFrames = filesize / (width*height)
// Height, width must be provided for YUV files, unused for BMP input (detected from header)
// YUV multiframes must be contained in single YUV file
//bool DetectNumberOfFrames(const char *fileName, FileType fileType,
//	unsigned int height, unsigned int width, int *numFrames, int *numSubFrames, char *baseFileName)
bool DetectNumberOfFrames(ImageFileInfo *imageFileInfo)
{
	imageFileInfo->numFrames = imageFileInfo->numSubFrames = 0;

	char fileExtension[5];
	switch (imageFileInfo->fileType)
	{
	case BMP_FILE:
		strcpy(fileExtension, "bmp");
		break;
	case YUV_FILE:
	default:
		strcpy(fileExtension, "yuv");
	}

	//TODO: Debug multiple frame detection
	//TODO: Add start frame detection
	//TODO: Allow variable number of digits in filenames

	// Parse filename to get base name
	const char *pChar = strrchr(imageFileInfo->filename, '.'); // find last '.' in string; letters afterwards should be extension
	// Find location of last digits in filename, if any
	const char *pCharDigitStart = pChar;
	do
	{
		pCharDigitStart--;
	} while ((pCharDigitStart >= imageFileInfo->filename) && isdigit(*pCharDigitStart));
	pCharDigitStart++;
	if (pChar - pCharDigitStart > 0)
	{
		long filenameDigits = pChar - pCharDigitStart;
		// Strip out trailing digits to find base filename
		strncpy(imageFileInfo->baseFileName, imageFileInfo->filename, pCharDigitStart - imageFileInfo->filename);
		imageFileInfo->baseFileName[pCharDigitStart - imageFileInfo->filename] = '\0';	// Terminate substring

		char fileDigits[MAX_STRING_LENGTH];
		strncpy(fileDigits, pCharDigitStart, filenameDigits);
		imageFileInfo->startFrame = atoi(fileDigits);

		// Find number of frames by checking existance of each filename in sequence
		int flagFileExists = 0;
		char fullFileName[MAX_STRING_LENGTH];
		do
		{
			sprintf(fullFileName, "%s%05d.%s", imageFileInfo->baseFileName, 
				imageFileInfo->startFrame + imageFileInfo->numFrames, fileExtension);
			flagFileExists = FileExists(fullFileName);
			if (flagFileExists)
				(imageFileInfo->numFrames)++;
		} while (flagFileExists);
		if (imageFileInfo->numFrames == 0)
		{
			fprintf(stderr, "ERROR Utils::DetectNumberOfFrames(). File %s cannot be found!\n", fullFileName);
			return FALSE;
		}
		//(imageFileInfo->numFrames)--;
	}
	else
		imageFileInfo->numFrames = 1; // Single file only

	if (imageFileInfo->numFrames == 1)
	{
		imageFileInfo->startFrame = 0;

		// Strip out extension to find base filename
		strncpy(imageFileInfo->baseFileName, imageFileInfo->filename, pChar - imageFileInfo->filename);
		imageFileInfo->baseFileName[pChar - imageFileInfo->filename] = '\0';	// Terminate substring

		if (imageFileInfo->fileType == YUV_FILE)
		{
			// Single file only. Check if there are multiple frames contained.
			// NOTE: This assumes file has no header
			// NOTE: This assumes file is YUV420, and that the height and width are correctly specified on the command line

			// Check that height, width specified
			if ((imageFileInfo->height == 0) || (imageFileInfo->width == 0))
			{
				fprintf(stderr, "ERROR Utils::DetectNumberOfFrames(). Height and width must be specified for YUV input!\n");
				return FALSE;
			}

			// Get file size in bytes.
			FILE *file;
			long sizeInBytes = 0;
			if ((file = fopen(imageFileInfo->filename, "rb")) != NULL)
			{
				fseek(file, 0L, SEEK_END);
				sizeInBytes = ftell(file);
				fclose(file);
			}
			else
			{
				fprintf(stderr, "ERROR Utils::DetectNumberOfFrames(). File %s cannot be found!\n", imageFileInfo->filename);
				return FALSE;
			}
			// NumFrames = sizeInBytes*8/(BPP_YUV420*Width*Height);
			long divisor = BPP_YUV420*imageFileInfo->width*imageFileInfo->height / 8;
			if (sizeInBytes % divisor != 0)
			{
				fprintf(stderr, "ERROR Utils::DetectNumberOfFrames(). YUV File %s header size is nonzero!\n", imageFileInfo->filename);
				return FALSE;
			}
			imageFileInfo->numSubFrames = (unsigned int)(sizeInBytes / divisor);
		}
	}

	return TRUE;
}

// ---------------------------------------
// Image format-specific file i/o routines
// ---------------------------------------

// Detect bitmap image size
bool DetectBmpImageSize(const char *fileName, int *width, int *height)
{
	FILE *file;
	BitmapFileHeader bmpHeader;

	file = fopen(fileName, "rb");
	if (file == NULL)
	{
		fprintf(stderr, "ERROR UTILS::DetectBmpImageSize(): File %s not found! \n", fileName);
		return FALSE;
	}

	// Read bitmap header info
	if (fread(&bmpHeader, sizeof(BitmapFileHeader), 1, file) != 1)
	{
		fprintf(stderr, "ERROR UTILS::DetectBmpImageSize(): Input BMP is corrupted! \n");
		return FALSE;
	}

	*width = abs(bmpHeader.bitmapWidth);
	*height = abs(bmpHeader.bitmapHeight);
	fclose(file);

	return TRUE;
}

// Read image in Bitmap file format
// pImage->colorSpace can be anything since file is read in in RGB 4:4:4 format, converted if necessary at end
bool LoadBmpImage(const char *fileName, IMAGE *pImage)
{
	FILE *file = fopen(fileName, "rb");
	if (file == NULL)
	{
		fprintf(stderr, "ERROR UTILS::LoadBmpImage(): Could not open file %s\n", fileName);
		return FALSE;
	}

	// Read bitmap header info
	BitmapFileHeader bmpHeader;
	if (fread(&bmpHeader, sizeof(BitmapFileHeader), 1, file) != 1)
	{
		// Error reading bmp header. Output error log and skip frame.
		fprintf(stderr, "ERROR UTILS::LoadBmpImage(): Could not read BMP header! \n");
		return FALSE;
	}

	// Check color depth is correct
	if (bmpHeader.colorDepth != 24)
	{
		// Incorrect color depth (only 24bit color supported). Output error log and skip frame.
		fprintf(stderr, "ERROR UTILS::LoadBmpImage(): Input BMP is not 24 bits. Only 24 bit BMP images supported.\n");
		return FALSE;
	}

	unsigned short width = (unsigned short)abs(bmpHeader.bitmapWidth);
	unsigned short height = (unsigned short)abs(bmpHeader.bitmapHeight);

	// Check that supplied image has correct allocated space
	if ((pImage->height != height) || (pImage->width != width) ||
		(pImage->precision != BPP8))
	{
		// Supplied image dimensions not correct. Deallocate image and re-allocate with correct dimensions.
		DestroyImage(pImage);
		pImage->pixArray = Create3DArray(PIXEL, 3, height, width);
		if (pImage->pixArray == NULL)
		{
			fprintf(stderr, "ERROR UTILS::LoadBmpImage(): Error re-allocating image memory!\n");
			return FALSE;
		}
		pImage->height = height;
		pImage->width = width;
		pImage->precision = BPP8;
	}

	// Calculate number of padding bytes if line not a multiple of 4
	unsigned int padBytes = (4 - ((width * 3) & 0x0003)) & 0x0003;

	// Allocate input pixel buffer
	unsigned int bufSize = (width * 3 + padBytes) * height; //bmp data size

	PIXEL *dataBuffer;
	if ((dataBuffer = (PIXEL *)malloc(bufSize)) == NULL)
	{
		fprintf(stderr, "ERROR UTILS::LoadBmpImage(): Could not allocate input buffer!\n");
		fclose(file);
		return FALSE;
	}

	// Read pixel data from file
	if (fread(dataBuffer, bufSize, 1, file) != 1)
	{
		fprintf(stderr, "ERROR UTILS::LoadBmpImage(): Could not read BMP pixel data: file corrupted!\n");
		fclose(file);
		free(dataBuffer);
		return FALSE;
	}
	fclose(file);


	// Pixels normally stored "upside-down" with respect to normal image raster scan order
	// Uncompressed Windows bitmaps can also be stored top to bottom when the Image Height value is negative
	int vFlip = !(bmpHeader.bitmapHeight < 0);
	PIXEL *bufPtr = dataBuffer;
	PIXEL rgbPixel[3];


	if (vFlip)
	{
		for (int y = height - 1; y >= 0; y--)
		{
			for (int x = 0; x < width; x++)
			{
				rgbPixel[B_PLANE] = (PIXEL)(*bufPtr++);
				rgbPixel[G_PLANE] = (PIXEL)(*bufPtr++);
				rgbPixel[R_PLANE] = (PIXEL)(*bufPtr++);
				SetPixel(pImage, y, x, rgbPixel);
			}
			bufPtr += padBytes;
		}
	}
	else
	{
		for (int y = 0; y < height; y++)
		{
			for (int x = 0; x < width; x++)
			{
				rgbPixel[B_PLANE] = (PIXEL)(*bufPtr++);
				rgbPixel[G_PLANE] = (PIXEL)(*bufPtr++);
				rgbPixel[R_PLANE] = (PIXEL)(*bufPtr++);
				SetPixel(pImage, y, x, rgbPixel);
			}
			bufPtr += padBytes;
		}
	}
	free(dataBuffer);
	
	// Do color space conversion if necessary to colorspace defined by given pImage
	if (pImage->colorSpace != RGB)
	{
		IMAGE tempImage;

		// Allocate temp image
		// Colorspace is don't care since it will be overwritten by CopyImage
		tempImage = CreateImage(pImage->colorSpace, width, height);

		// Copy image to new temp image
		if (!CopyImage(pImage, &tempImage))
		{
			DestroyImage(&tempImage);
			fprintf(stderr, "UTILS::LoadBmpImage(): Unable to copy image for color space conversion!\n");
			return FALSE;
		}

		// Convert color space
		tempImage.colorSpace = RGB;
		if (!ConvertImage(&tempImage, pImage))
		{
			DestroyImage(&tempImage);
			fprintf(stderr, "UTILS::LoadBmpImage(): Unable to convert image color space!\n");
			return FALSE;
		}

		// Deallocate temp image
		DestroyImage(&tempImage);
	}

	return TRUE;
}

// Writes image in Bitmap file format
bool SaveBmpImage(const char *fileName, IMAGE *pImage)
{
	IMAGE tempImage = CreateImage(RGB, pImage->width, pImage->height);

	// Color space conversion if necessary
	if (pImage->colorSpace != RGB)
		ConvertImage(pImage, &tempImage);
	else
		CopyImage(pImage, &tempImage);

	FILE *file = fopen(fileName, "wb");
	if (file == NULL)
	{
		fprintf(stderr, "ERROR UTILS::SaveBmpImage(): Could not create file %s!\n", fileName);
		return FALSE;
	}

	// Calculate number of padding bytes if line not a multiple of 4
	unsigned int padBytes = (4 - ((tempImage.width * 3) & 0x0003)) & 0x0003;

	// Allocate input pixel buffer
	unsigned int bufSize = (tempImage.width * 3 + padBytes) * tempImage.height; //bmp data size

	// Clear bitmap header to 0's
	BitmapFileHeader bmpHeader;
	memset(&bmpHeader, 0, sizeof(BitmapFileHeader));

	// Initialize bitmap header fields
	bmpHeader.fileType = 0x4D42;
	bmpHeader.fileSize = bufSize + sizeof(BitmapFileHeader); // file size in bytes
	bmpHeader.reserved1 = 0;
	bmpHeader.dataOffset = sizeof(BitmapFileHeader); // offset to pixel data
	bmpHeader.headerSize = 40;
	bmpHeader.bitmapWidth = tempImage.width;
	bmpHeader.bitmapHeight = tempImage.height;
	bmpHeader.numPlanes = 1;
	bmpHeader.colorDepth = 24;
	bmpHeader.bitmapSize = bufSize;

	// Allocate bitmap data buffer
	bufSize += sizeof(BitmapFileHeader);
	PIXEL *dataBuffer;
	if ((dataBuffer = (PIXEL *)malloc(bufSize)) == NULL)
	{
		fprintf(stderr, "ERROR UTILS::SaveBmpImage(): Could not allocate bitmap data buffer!\n");
		fclose(file);
		return FALSE;
	}

	// Copy header into data buffer
	memcpy(dataBuffer, &bmpHeader, sizeof(BitmapFileHeader));
	PIXEL *bufPtr;
	bufPtr = dataBuffer + sizeof(BitmapFileHeader);

	for (int y = tempImage.height - 1; y >= 0; y--)	// Output bot->top
	{
		for (int x = 0; x < tempImage.width; x++)
		{
			PIXEL rgbPixel[3];
			GetPixel(&tempImage, y, x, REPEAT, rgbPixel);

			*bufPtr++ = (PIXEL)(rgbPixel[B_PLANE]);
			*bufPtr++ = (PIXEL)(rgbPixel[G_PLANE]);
			*bufPtr++ = (PIXEL)(rgbPixel[R_PLANE]);
		}
		bufPtr += padBytes;
	}

	// Write data buffer to file
	fwrite(dataBuffer, bufSize, 1, file);

	// Cleanup
	fclose(file);
	free(dataBuffer);
	DestroyImage(&tempImage);

	return TRUE;
}

// Reads image in raw YUV file format
// Currently support YUV420 only, not YUV 422 or YUV444
// Can also internally convert YUV420 to RGB if pImage->colorSpace == RGB
bool LoadRawYUVImage(const char *fileName, IMAGE *pImage, int subFrame, YUVType fileSubtype)
{
	ColorSpaces inputColorSpace = pImage->colorSpace;
	switch (inputColorSpace)
	{
	case RGB:
		// Must set image colorspace to YUV420 so that SetSubPixel() addresses correct locations
		// This will be set back to RGB below during color space conversion
		pImage->colorSpace = YUV420;
		break;
	case YUV420:
		break;
	default:
		fprintf(stderr, "ERROR UTILS::LoadRawYUVImage(): Unsupported color space!\n");
		return FALSE;
	}

	FILE *file = fopen(fileName, "rb");
	if (file == NULL)
	{
		fprintf(stderr, "ERROR UTILS::LoadRawYUVImage(): Could not open file %s\n", fileName);
		return FALSE;
	}

	// Go to appropriate location for start of subframe data
	long seekLocation = BPP_YUV420 * pImage->width * pImage->height * subFrame / 8;
	fseek(file, seekLocation, SEEK_SET);

	// Read YUV data in order depending on fileSubType
	// Read Y plane
	// Allocate input pixel buffer
	unsigned int bufSize = pImage->width * pImage->height;	// Y plane size
	PIXEL *dataBuffer;
	if ((dataBuffer = (PIXEL *)malloc(bufSize)) == NULL)
	{
		fprintf(stderr, "ERROR UTILS::LoadRawYUVImage(): Could not allocate Y buffer!\n");
		fclose(file);
		return FALSE;
	}

	// Read pixel data from file
	if (fread(dataBuffer, bufSize, 1, file) != 1)
	{
		fprintf(stderr, "ERROR UTILS::LoadRawYUVImage(): Could not read Y pixel data: file corrupted!\n");
		fclose(file);
		free(dataBuffer);
		// Return instead of exit so we can try to read next frame
		return FALSE;
	}

	// Write Y plane to image
	PIXEL *bufPtr = dataBuffer;
	for (int y = 0; y < pImage->height; y++) {
		for (int x = 0; x < pImage->width; x++) {
			SetSubPixel(pImage, y, x, Y_PLANE, (PIXEL)(*bufPtr++));
		}
	}
	free(dataBuffer);

	// Read UV planes
	// Allocate input pixel buffer
	bufSize /= 2;	// UV plane size = (w*h/4)*2
	if ((dataBuffer = (PIXEL *)malloc(bufSize)) == NULL)
	{
		fprintf(stderr, "ERROR UTILS::LoadRawYUVImage(): Could not allocate UV buffer!\n");
		fclose(file);
		return FALSE;
	}

	// Read UV pixel data from file
	if (fread(dataBuffer, bufSize, 1, file) != 1)
	{
		fprintf(stderr, "ERROR UTILS::LoadRawYUVImage(): Could not read UV pixel data: file corrupted!\n");
		fclose(file);
		free(dataBuffer);
		// Return instead of exit so we can try to read next frame
		return FALSE;
	}
	fclose(file);
	bufPtr = dataBuffer;

	// Write UV planes to image
	YUVPlanes plane1,plane2;

	// Setup order of U, V planes within specified file format
	switch (fileSubtype)
	{
	case YUV420_I420:
	case YUV420_NV12:
		plane1 = U_PLANE;
		plane2 = V_PLANE;
		break;
	case YUV420_YV12:
	case YUV420_NV21:
		plane1 = V_PLANE;
		plane2 = U_PLANE;
		break;
	default:
		fprintf(stderr, "ERROR UTILS::LoadRawYUVImage(): Invalid YUV format type!\n");
		free(dataBuffer);
		return FALSE;
	}

	switch (fileSubtype)
	{
	case YUV420_I420:
	case YUV420_YV12:
		for (int y = 0; y < pImage->height; y += 2) {
			for (int x = 0; x < pImage->width; x += 2) {
				SetSubPixel(pImage, y, x, plane1, (PIXEL)(*bufPtr++));
			}
		}
		for (int y = 0; y < pImage->height; y += 2) {
			for (int x = 0; x < pImage->width; x += 2) {
				SetSubPixel(pImage, y, x, plane2, (PIXEL)(*bufPtr++));
			}
		}
		break;
	case YUV420_NV12:
	case YUV420_NV21:
		for (int y = 0; y < pImage->height; y += 2) {
			for (int x = 0; x < pImage->width; x += 2) {
				SetSubPixel(pImage, y, x, plane1, (PIXEL)(*bufPtr++));
				SetSubPixel(pImage, y, x, plane2, (PIXEL)(*bufPtr++));
			}
		}
		break;
	default:
		fprintf(stderr, "ERROR UTILS::LoadRawYUVImage(): Invalid YUV format type!\n");
		free(dataBuffer);
		return FALSE;
	}
	free(dataBuffer);

	// Do color space conversion if necessary to colorspace defined by given pImage
	if (inputColorSpace == RGB)
	{
		IMAGE tempImage;

		// Allocate temp image
		// Colorspace is don't care since it will be overwritten by CopyImage
		tempImage = CreateImage(inputColorSpace, pImage->width, pImage->height);

		// Copy image to new temp image
		if (!CopyImage(pImage, &tempImage))
		{
			DestroyImage(&tempImage);
			fprintf(stderr, "UTILS::LoadRawYUVImage(): Unable to copy image for color space conversion!\n");
			return FALSE;
		}

		// Convert color space
		pImage->colorSpace = RGB;
		if (!ConvertImage(&tempImage, pImage))
		{
			DestroyImage(&tempImage);
			fprintf(stderr, "UTILS::LoadRawYUVImage(): Unable to convert image color space!\n");
			return FALSE;
		}

		// Deallocate temp image
		DestroyImage(&tempImage);
	}

	return TRUE;
}

// Writes image in raw YUV file format
bool SaveRawYUVImage(const char *fileName, IMAGE *pImage, YUVType fileSubtype)
{
	FILE *file = fopen(fileName, "a+b");
	if (file == NULL)
	{
		fprintf(stderr, "ERROR UTILS::SaveRawYUVImage(): Could not open file %s\n", fileName);
		return FALSE;
	}

	// Write YUV data in order depending on fileSubType
	// Write Y plane
	// Allocate pixel buffer
	unsigned int bufSize = pImage->width * pImage->height;	// Y plane size
	PIXEL *dataBuffer;
	if ((dataBuffer = (PIXEL *)malloc(bufSize)) == NULL)
	{
		fprintf(stderr, "ERROR UTILS::SaveRawYUVImage(): Could not allocate Y buffer!\n");
		fclose(file);
		return FALSE;
	}

	// Write Y plane to buffer
	PIXEL *bufPtr = dataBuffer;
	for (int y = 0; y < pImage->height; y++) {
		for (int x = 0; x < pImage->width; x++) {
			*bufPtr++ = GetSubPixel(pImage, y, x, REPEAT, Y_PLANE);
		}
	}

	// Write pixel data to file
	fwrite(dataBuffer, bufSize, 1, file);

	free(dataBuffer);

	// Read UV planes
	// Allocate input pixel buffer
	bufSize = (pImage->width * pImage->height) / 2;	// UV plane size = (w*h/4)*2
	if ((dataBuffer = (PIXEL *)malloc(bufSize)) == NULL)
	{
		fprintf(stderr, "ERROR UTILS::SaveRawYUVImage(): Could not allocate UV buffer!\n");
		fclose(file);
		return FALSE;
	}
	bufPtr = dataBuffer;

	// Write UV planes to buffer
	int plane1, plane2;

	// Setup order of U, V planes within specified file format
	switch (fileSubtype)
	{
	case YUV420_I420:
	case YUV420_NV12:
		plane1 = U_PLANE;
		plane2 = V_PLANE;
		break;
	case YUV420_YV12:
	case YUV420_NV21:
		plane1 = V_PLANE;
		plane2 = U_PLANE;
		break;
	default:
		fprintf(stderr, "ERROR UTILS::SaveRawYUVImage(): Invalid YUV format type!\n");
		fclose(file);
		free(dataBuffer);
		// Return instead of exit so we can try to read next frame
		return FALSE;
	}

	switch (fileSubtype)
	{
	case YUV420_I420:
	case YUV420_YV12:
		for (int y = 0; y < pImage->height; y += 2) {
			for (int x = 0; x < pImage->width; x += 2) {
				*bufPtr++ = GetSubPixel(pImage, y, x, REPEAT, plane1);
			}
		}
		for (int y = 0; y < pImage->height; y += 2) {
			for (int x = 0; x < pImage->width; x += 2) {
				*bufPtr++ = GetSubPixel(pImage, y, x, REPEAT, plane2);
			}
		}
		break;
	case YUV420_NV12:
	case YUV420_NV21:
		for (int y = 0; y < pImage->height; y += 2) {
			for (int x = 0; x < pImage->width; x += 2) {
				*bufPtr++ = GetSubPixel(pImage, y, x, REPEAT, plane1);
				*bufPtr++ = GetSubPixel(pImage, y, x, REPEAT, plane2);
			}
		}
		break;
	default:
		fprintf(stderr, "ERROR UTILS::SaveRawYUVImage(): Invalid YUV format type!\n");
		fclose(file);
		free(dataBuffer);
		// Return instead of exit so we can try to read next frame
		return FALSE;
	}

	// Write pixel data to file
	fwrite(dataBuffer, bufSize, 1, file);

	free(dataBuffer);

	fclose(file);
	return TRUE;
}
