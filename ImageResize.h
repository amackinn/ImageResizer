// ImageResize.h, lanczos image resizer v1.00, Andrew MacKinnon andrewmackinnon@rogers.com
// See MIT_License.txt

#ifndef IMAGERESIZE_H_
#define IMAGERESIZE_H_

#include "Utils.h"

#define MIN_WIDTH	1
#define MAX_WIDTH	4096
#define MIN_HEIGHT	1
#define MAX_HEIGHT	4096

typedef struct
{
	double scaleRatio;			// Scaling ratio output:input
	YUVType fileSubtype;		// FOURCC type of YUV file
	int height;		// Input file height. Supplied on command line for YUV files only
	int width;			// Input file width. Supplied on command line for YUV files only
	const char *inFilename;		// Input file name
	const char *outFilename;	// Output file name
	EdgeMethod edgeMethod;		// Edge handling method
	double gamma;				// Gamma value used to linearize pixel data
} CmdLineParms;

// TODO: convert c-style struct to C++ class
typedef struct
{
	double **filterWeights;		// Filter weights
	int **contribPixPos;		// Position of contributing pixels
	int *numContribPixels;		// Number of contributors for target pixel
	double *weightsSum;			// Sum of weights for target pixel
} ContribTable;

#endif //#ifndef LANCZOS_RESIZE_H_
