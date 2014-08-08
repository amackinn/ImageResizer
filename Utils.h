// Utils.h, image processing utilities v1.00, Andrew MacKinnon andrewmackinnon@rogers.com
// See MIT_License.txt

#ifndef IMAGERESIZE_UTILS_H_
#define IMAGERESIZE_UTILS_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/******************************************************************************
* MACROS
*****************************************************************************/
#define CLAMP(x,a,b) ((x)<(a) ? (a) : (x)>(b) ? (b) : (x))
#define MIN(a,b) ((a)< (b) ? (a) : (b))
#define MAX(a,b) ((a)>=(b) ? (a) : (b))

#ifdef _WIN32
#define PATH_SEPARATOR '\\'
#define FCLOSEALL()             _fcloseall()
#else	// Unix, linux, MACOS
#define PATH_SEPARATOR '/'
#define FCLOSEALL()              fcloseall()  
#endif


/******************************************************************************
* DEFINES
*****************************************************************************/
typedef unsigned char PIXEL;

#define MAX_STRING_LENGTH		256

#define TRUE					1
#define FALSE					0

#define FWD_GAMMA_LUTSIZE			256		// =2^8. Only 8 bpp input file supported
#define BWD_GAMMA_LUTSIZE			4096	// =2^12. bpp after input gamma correction is removed. 12=BPPIN + 4
// to account for greater resolution needed


// Max value of 8-bit pixel value
const int PIXMAX = 255;

// Max value of pixel in floating piont
const double DBLPIXMAX = 1.0;

// Supported image formats
enum FileType
{
	YUV_FILE,	// YUV files (.yuv).
	BMP_FILE,	// Bitmap files (.bmp).
	UNSUPPORTED_FILE
};

// Supported YUV420 file formats
// TODO: Add YUV422 support
enum YUVType
{
	NO_SUBTYPE,
	YUV420_I420,	// I420 Y*8*n bits, U*2*n bits, V*2*n bits for n-pixel frame
	YUV420_YV12,	// I420 Y*8*n bits, V*2*n bits, U*2*n bits for n-pixel frame
	YUV420_NV12,	// I420 Y*8*n bits, (UV)*2*n bits for n-pixel frame
	YUV420_NV21		// I420 Y*8*n bits, (VU)*2*n bits for n-pixel frame
};

#define BPP_YUV420				12 // Bits per pixel for YUV420

// Color spaces. Included for future expandability.
enum ColorSpaces
{
	RGB,		// Standard RGB.
	YUV444,		// YUV 4:4:4.
	YUV422,		// YUV 4:2:2.
	YUV420		// YUV 4:2:0.
};

// Color planes
enum RGBPlanes
{
	R_PLANE,
	G_PLANE,
	B_PLANE
};
enum YUVPlanes
{
	Y_PLANE,
	U_PLANE,
	V_PLANE
};

// Edge handling types
enum EdgeMethod
{
	REPEAT,			// Replicate the edge pixel.
	MIRROR,			// Mirror about the edge.
	NOCONTRIB		// Shorten filter kernal to zero out weights out of image
};

// Pixel Precision, 8bpp or double
enum PixelPrecision
{
	BPP8,			// The usual default pixel type for gamma-corrected display pixels
	DOUBLE			// Used for de-gamma'ed pixels
};

// Structure used to hold a still image.
typedef struct
{
	ColorSpaces colorSpace;		// The color space, per enum ColorSpaces
	int height;					// Height of the image in lines
	int width;					// Width of the image in pixels
	PixelPrecision precision;	// Pixel Precision, 8bpp or double
	PIXEL ***pixArray;			// 3 plane pixel buffer, allocated if precision==BPP8
	double ***dblPixArray;		// 3 plane double precision pixel buffer, allocated only if precision==DOUBLE
} IMAGE;

typedef struct
{
	FileType fileType;				// BMP or YUV
	YUVType fileSubtype;			// YUV FOURCC type
	int height;
	int width;
	int numFrames;
	int numSubFrames;
	int startFrame;
	const char *filename;
	char baseFileName[MAX_STRING_LENGTH];
} ImageFileInfo;

/******************************************************************************
* PUBLIC FUNCTIONS
*****************************************************************************/

// -----------------------------
// Array alloc/dealloc functions
// -----------------------------
// Macro access to functions taking void pointers to generalize to any data type
// for futureproofing
#define Create2DArray(dataType, y, x) (dataType**)Alloc2DArray(sizeof(dataType), y, x)
#define Destroy2DArray(arrayName) Free2DArray((void**)arrayName)

// Allocate 2D array storage to allow array-type [][] addressing
void **Alloc2DArray(int typeSize, int y, int x);

// Deallocate 2D array memory
void Free2DArray(void ** array2D);

#define Create3DArray(dataType, z, y, x) (dataType***)Alloc3DArray(sizeof(dataType), z, y, x)
#define Destroy3DArray(arrayName) Free3DArray((void***)arrayName)

// Allocate 3D array storage to allow array-type [][][] addressing
void ***Alloc3DArray(int typeSize, int z, int y, int x);

// Deallocate 3D array memory
void Free3DArray(void *** array3D);

// ---------------------------
// Image manipulation routines
// ---------------------------

// Allocates storage for and initializes image structure and returns pointer to new image
IMAGE CreateImage(ColorSpaces colorSpace, int width, int height);
IMAGE CreateImage(ColorSpaces colorSpace, int width, int height, PixelPrecision precision);

// Deallocates image previously created with CreateImage();
void DestroyImage(IMAGE *pImage);

// Copies entire image from first image to second
bool CopyImage(const IMAGE *pImageIn, IMAGE * pImageOut);

// Converts pixels of first image into color space of second image
bool ConvertImage(const IMAGE *pImageIn, IMAGE *pImageOut);

// Takes gamma-corrected pImageIn, applies supplied fwdGamma table to convert to linear light pImageOut
// Y'UV in YUV out, or R'G'B' in RGB out
bool DegammaImage(const IMAGE *pImageIn, IMAGE *pImageOut, double fwdGamma[]);

// Takes gamma-corrected pImageIn, applies supplied fwdGamma table to convert to linear light pImageOut
// YUV in Y'UV out, or RGB in R'G'B' out
bool GammaImage(const IMAGE *pImageIn, IMAGE *pImageOut, PIXEL bwdGamma[]);

// Gets YUV or RGB pixel from image
// x, y co-ordinates are internally divided down for YUV422/YUV420 UV planes
bool GetPixel(const IMAGE *pImage, int y, int x, const EdgeMethod edgeMethod, PIXEL pixel[]);
bool GetPixel(const IMAGE *pImage, int y, int x, const EdgeMethod edgeMethod, double pixel[]);

// Gets subpixel (R, G, B, Y, U, or V)
// x, y co-ordinates are internally divided down for YUV422/YUV420 UV planes
PIXEL GetSubPixel(const IMAGE *pImage, int y, int x, const EdgeMethod edgeMethod, const int plane);

// Gets subpixel (R, G, B, Y, U, or V)
// x, y co-ordinates are internally divided down for YUV422/YUV420 UV planes
// NOTE: This function has no edge handling and does not check the supplied pixel address
//       against the iamge boundaries. Do not use if there is any chance of going outside!
PIXEL GetSubPixel(const IMAGE *pImage, int y, int x, const int plane);

// Sets YUV or RGB pixel from image
// x, y co-ordinates are internally divided down for YUV422/YUV420 UV planes
void SetPixel(const IMAGE *pImage, int y, int x, PIXEL pixel[]);

// Sets subpixel (R, G, B, Y, U, or V)
// x, y co-ordinates are internally divided down for YUV422/YUV420 UV planes
void SetSubPixel(const IMAGE *pImage, int y, int x, const int plane, PIXEL pixVal);

// Adjust input address i based on edgeMethod. 
// 1D, either H/V depending on whether max H/D supplied as imageDimMax
int HandleEdgeCase(int i, int imageDimMax, EdgeMethod edgeMethod);

// Divide down x,y addresses for plane[1] and plane [2]
void HandleColorspaceAddress(int *x, int *y, ColorSpaces colorSpace);

// ---------------------------
// General image file I/O
// ---------------------------

// Detects if file exists
bool FileExists(const char *fileName);

// Determine file type by file extension.
bool DetectFileType(const char *fileName, FileType* fileType);

// Determine how many frames in YUV file or BMP sequence
bool DetectNumberOfFrames(ImageFileInfo *imageFileInfo);

// ---------------------------------------
// Image format-specific file i/o routines
// ---------------------------------------

// Detect bitmap image size
bool DetectBmpImageSize(const char *fileName, int *width, int *height);

// Reads image in Bitmap file format
bool LoadBmpImage(const char *fileName, IMAGE *pImage);

// Writes image in Bitmap file format
bool SaveBmpImage(const char *fileName, IMAGE *pImage);

// Reads image in raw YUV420 file format
// TODO: Add YUV422 support
bool LoadRawYUVImage(const char *fileName, IMAGE *pImage, int subFrame, YUVType fileSubtype);

// Writes image in raw YUV420 file format
// TODO: Add YUV422 support
bool SaveRawYUVImage(const char *fileName, IMAGE *pImage, YUVType fileSubtype);




#endif // #ifndef LANCZOS_UTILS_H_