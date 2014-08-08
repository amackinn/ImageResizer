// ImageResize.cpp, lanczos image resizer v1.00, Andrew MacKinnon andrewmackinnon@rogers.com
// See MIT_License.txt

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <float.h>
#include <ctype.h>
#include "ImageResize.h"
#include "Utils.h"

#define M_PI				3.14159265358979323846
#define EPSILON				.0000125
#define LANCZOS2_NUMTAPS	2.0

// Private functions
static void print_usage();
static bool GetFileInfo(ImageFileInfo *inFileInfo, ImageFileInfo *outFileInfo);
static bool ParseCmdLine(const int argc, char *argv[], CmdLineParms *parms);
static double sinc(double x);
static double lanczos2Filter(double in);
static bool MakeContribTable(ContribTable *contribTable, int inDimSize, 
	int outDimSize, EdgeMethod edgeMethod);
static void DestroyContribTable(ContribTable *contribTable);
static bool ResizeImage(const IMAGE *pImageIn, IMAGE *pImageOut, EdgeMethod edgeMethod);
static void MainCleanup(IMAGE *pImageIn, IMAGE *pImageOut, IMAGE *pImageInLinear, IMAGE *pImageOutLinear);

// Output usage and exit indicating failure
static void print_usage()
{
	printf("ImageResize [options] <source_file> <dest_file>\n");
	printf("\nRequired parameters (must follow options):\n");
	printf("source_file: Source image file, in yuv I420 (.yuv) or BMP (.bmp) format.\n");
	printf("dest_file: Destination image file, in yuv I420 (.yuv) or BMP (.bmp) format.\n");
	printf("\nOptions:\n");
	printf("-g <gamma>: Gamma value. Set to 1.0 to disable. Default = 2.2\n");
	printf("-r[1|2]: H/V scaling ratio.\n");
	printf("\t-r1: Upscale 2x < default > \n");
	printf("\t-r2: Shrink 1/2x\n");
	printf("-h <height in lines>: MUST be specified if input is YUV file\n");
	printf("-w <width in pixels>: MUST be specified if input is YUV file\n");
	printf("-y <color format>: YUV file format.\n");
	printf("\tYUV file format: \n");
	printf("\t\t0 = YUV420_I420(default), 1 = YUV420_YV12, 2 = YUV420_NV12, 3 = YUV420_NV21");
	printf("\n\nExamples of usage:\n");
	printf("ImageResize -g 1.8 -w 528 -h 488 -r2 a_528x488_avg.yuv a_264x244_avg.yuv\n");
	printf("\tShrink YUV420 I420 input by half, using Pre-Mac OS X v10.6 Snow Leopard gamma value\n\n");
	printf("ImageResize -g 1.0 -r1 birds.bmp birds_352x288.yuv\n");
	printf("\tExpand QCIF-sized bmp by 2x without gamma compensation, output to YUV420 I420\n");

	exit(EXIT_FAILURE);
}

// Detect file information including:
// File type (BMP, YUV), according to extension. If extension not recognized, 
//		try to read BMP header to detect BMP
// Image dimensions (BMP only)
// Number of frames in sequence
static bool GetFileInfo(ImageFileInfo *inFileInfo, ImageFileInfo *outFileInfo)
{
	// Check input image exists
	if (!FileExists(inFileInfo->filename))
	{
		fprintf(stderr, "Input file %s cannot be opened!\n", inFileInfo->filename);
		return FALSE;
	}

	// Determine image types
	if (!DetectFileType(inFileInfo->filename, &inFileInfo->fileType))
	{
		if (DetectBmpImageSize(inFileInfo->filename, &inFileInfo->width, &inFileInfo->height))
			// If no extension, try detecting BMP file using header info.
			inFileInfo->fileType = BMP_FILE;
		else
			// Otherwise default to YUV file
			inFileInfo->fileType = YUV_FILE;
	}

	// If output filename has extension, determine file type from that
	if (!DetectFileType(outFileInfo->filename, &outFileInfo->fileType))
		// Otherwise default to output file same as input file to avoid color space conversion
		outFileInfo->fileType = inFileInfo->fileType;

	// If input is BMP, get dimensions from header
	// If it's YUV, dimensions must have already been supplied from command line
	if (inFileInfo->fileType == BMP_FILE)
	{
		if (!DetectBmpImageSize(inFileInfo->filename, &inFileInfo->width, &inFileInfo->height))
		{
			fprintf(stderr, "Cannot determine BMP dimensions!\n");
			return FALSE;
		}
	}
	else if (inFileInfo->width == 0 || inFileInfo->height == 0)
	{
		fprintf(stderr, "Height and width must be supplied when input file is YUV!\n");
		print_usage();
	}

	// Determine number of frames in sequence
	if (!DetectNumberOfFrames(inFileInfo))
	{
		fprintf(stderr, "Cannot determine number of frames in file %s!\n", inFileInfo->filename);
		return FALSE;
	}
	if ((inFileInfo->fileType == YUV_FILE) && (outFileInfo->fileType == BMP_FILE))
	{
		outFileInfo->numFrames = inFileInfo->numFrames * inFileInfo->numSubFrames;
	}
	else
	{
		outFileInfo->numFrames = inFileInfo->numFrames;
		outFileInfo->numSubFrames = inFileInfo->numSubFrames;
	}
	outFileInfo->startFrame = inFileInfo->startFrame;

	// Parse output filename
	// Parse filename to get base name
	const char *pChar = strrchr(outFileInfo->filename, '.');
	// Strip out extension to find base filename
	strncpy(outFileInfo->baseFileName, outFileInfo->filename, pChar - outFileInfo->filename);
	outFileInfo->baseFileName[pChar - outFileInfo->filename] = '\0';	// Terminate substring

	return TRUE;
}

// Parse command line
static bool ParseCmdLine(const int argc, char *argv[], CmdLineParms *parms)
{
	int arg_index = 1;
	while ((arg_index < argc) && (argv[arg_index][0] == '-'))
	{
		switch (tolower(argv[arg_index][1]))
		{
		case 'r':
			if (argv[arg_index][2] == '1')
				parms->scaleRatio = 2.0f;
			else if (argv[arg_index][2] == '2')
				parms->scaleRatio = 0.5f;
			else if (argv[arg_index][2] == '0')
				parms->scaleRatio = 1.0f;
			else
			{
				fprintf(stderr, "Unrecognized scaling ratio.\n");
				print_usage();
			}
			break;
		case 'h':
			parms->height = atoi(argv[++arg_index]);
			if (parms->height == 0)
			{
				fprintf(stderr, "Unrecognized height or height specified as 0.\n");
				print_usage();
			}
			break;
		case 'w':
			parms->width = atoi(argv[++arg_index]);
			if (parms->width == 0)
			{
				fprintf(stderr, "Unrecognized width or width specified as 0.\n");
				print_usage();
			}
			break;
		case 'g':
			parms->gamma = atof(argv[++arg_index]);
			if (parms->gamma == 0.0)
			{
				fprintf(stderr, "Unrecognized gamma or gamma specified as 0.\n");
				print_usage();
			}
			break;
		case 'y':
			parms->fileSubtype = (YUVType)(atoi(argv[++arg_index]) + 1);
			if ((parms->fileSubtype < YUV420_I420) || (parms->fileSubtype < YUV420_NV21))
			{
				fprintf(stderr, "Unrecognized YUV color format.\n");
				print_usage();
			}
			break;
		default:
			fprintf(stderr, "Unrecognized option: %s\n", argv[arg_index]);
			print_usage();
		}
		arg_index++;
	}
	if (argc < (arg_index + 2))
	{
		fprintf(stderr, "Missing required parameters.\n");
		print_usage();
	}
	parms->inFilename = argv[arg_index++];
	parms->outFilename = argv[arg_index++];

	return TRUE;
}

// sinc(x) function
static double sinc(double x) 
{
	x *= M_PI;

	if ((x < EPSILON) && (x > -EPSILON)) 
	{
		// Handle range near divide by zero
		return (1.0f + x*x*(-1 / 6.0f + x*x / 120.0f));
	}

	return sin(x) / x;
}

static double fabsThresh(double x, double thresh)
{
	if (fabs(x) < thresh)
		return 0.0;
	return x;
}

// Returns filter weight at position t
static double lanczos2Filter(double t)
{
	const double R = LANCZOS2_NUMTAPS;

	if (t < 0.0f)
		t = -t;

	// return windowed sinc based on number of lobes
	// Lanzos2 filter defined by lobes=2
	if (t < R)
		return fabsThresh(sinc(t)*sinc(t / R), EPSILON);
	else
		return (0.0f);
}

// 1D horizontal filter using contributor table
static void Filter1DHorz(const IMAGE *pImageIn, IMAGE *pImageOut,
	int x, int y, int plane, EdgeMethod edgeMethod, ContribTable contribs)
{
	double tmpResult = 0.0;
	for (int k = 0; k < contribs.numContribPixels[x]; k++)
	{
		double tmpPixel = pImageIn->dblPixArray[plane][y][contribs.contribPixPos[x][k]];
		tmpResult += contribs.filterWeights[x][k] * tmpPixel;
	}
	tmpResult /= contribs.weightsSum[x];
	double outPixel = CLAMP(tmpResult, 0, 1.0);
	pImageOut->dblPixArray[plane][y][x] = outPixel;
}

// 1D vertical filter using contributor table
static void Filter1DVert(const IMAGE *pImageIn, IMAGE *pImageOut,
	int x, int y, int plane, EdgeMethod edgeMethod, ContribTable contribs)
{
	double tmpResult = 0.0;
	for (int k = 0; k < contribs.numContribPixels[y]; k++)
	{
		double tmpPixel = pImageIn->dblPixArray[plane][contribs.contribPixPos[y][k]][x];
		tmpResult += contribs.filterWeights[y][k] * tmpPixel;
	}
	tmpResult /= contribs.weightsSum[y];
	double outPixel = CLAMP(tmpResult, 0, 1.0);
	pImageOut->dblPixArray[plane][y][x] = outPixel;
}


// Makes pixel contribution table
// Slight speed efficiency due to checking image boundaaries in O(n) time instead of every pixel O(n^2)
// Allows precomputation of arbitrary filter phases for arbitrary scaling ratios
static bool MakeContribTable(ContribTable *contribTable, int inDimSize, int outDimSize, EdgeMethod edgeMethod)
{
	double scaleRatio = (double)outDimSize / inDimSize;	// scale ratio

	double scaledHalfTaps;	// Max one-sided number of filter taps, depends on if up or downscaling
	double filterScale;		// 

	if (scaleRatio > 1.0)
	{
		// Horizontal upscaling
		filterScale = 1.0;
		scaledHalfTaps = LANCZOS2_NUMTAPS;
	}
	else
	{
		// Horizontal downscaling
		filterScale = scaleRatio;
		scaledHalfTaps = LANCZOS2_NUMTAPS / scaleRatio;
	}
	int maxTaps = (int)(2 * scaledHalfTaps + 1);

	contribTable->filterWeights = Create2DArray(double, outDimSize, maxTaps);	// filter weights
	contribTable->contribPixPos = Create2DArray(int, outDimSize, maxTaps);		// contributing pixels
	contribTable->numContribPixels = (int *)calloc(outDimSize, sizeof(int));		// number of contributors for target pixel
	contribTable->weightsSum = (double *)calloc(outDimSize, sizeof(double));		// sum of weights for target pixel

	if (!contribTable->filterWeights || !contribTable->contribPixPos ||
		!contribTable->numContribPixels || !contribTable->weightsSum)
	{
		fprintf(stderr, "ERROR: MakeContribTable(): Could not allocate memory for ContribTable!\n");
		DestroyContribTable(contribTable);
		return FALSE;
	}

	// Precalculate filter weights for each target pixel in output row
	// Number of contributing input pixels per output target pixel is variable depending on filter phase
	for (int i = 0; i < outDimSize; i++)
	{
		// Calculate extents of contributor pixels
		// Supports all scaling ratios, both shrink and expand
		double center = ((double)i + 0.5f) / scaleRatio - 0.5f;
		int left = (int)(floor(center - scaledHalfTaps));
		int right = (int)(ceil(center + scaledHalfTaps));

		for (int j = left; j <= right; j++)
		{
			// If edgeMethod == NOCONTRIB and contributing pixel lies outside iamge area, skip it
			// i.e. filter weight is 0
			if (edgeMethod == NOCONTRIB && (j<0 || j>(int)inDimSize))
				continue;

			double weight;
			if ((weight = lanczos2Filter((center - j) * filterScale)) == 0)
				continue;

			// Handle image edge cases
			int x = HandleEdgeCase(j, (int)inDimSize, edgeMethod);

			contribTable->filterWeights[i][contribTable->numContribPixels[i]] = weight;
			contribTable->contribPixPos[i][contribTable->numContribPixels[i]] = x;
			contribTable->weightsSum[i] += weight;
			contribTable->numContribPixels[i]++;
		}
	}

	return TRUE;
}

// Safely deallocate contributor table storage
static void DestroyContribTable(ContribTable *contribTable)
{
	if (contribTable->filterWeights)
		Destroy2DArray(contribTable->filterWeights);
	if (contribTable->contribPixPos)
		Destroy2DArray(contribTable->contribPixPos);
	if (contribTable->numContribPixels)
		free(contribTable->numContribPixels);
	if (contribTable->weightsSum)
		free(contribTable->weightsSum);
}

// Main rescaling function
// Currently hardcoded to 2D separable Lanczos2 filter
// Creates separate contributor table for Y, UV planes to facilitate image edge handling for
// differently sized YUV422/YUV420 chroma planes
// Note:Image scaling done in *Linear Light domain*, i.e. RGB or YUV,
//		not in linear perception domain (Y'UV or R'G'B'),
//		so gamma correction must be applied before & after this function.
//		Doing it this way makes for much better quality in dark regions, especially in shrink case.
static bool ResizeImage(const IMAGE *pImageIn, IMAGE *pImageOut, EdgeMethod edgeMethod)
{
	// In, out image same size: no rescaling
	if ((pImageIn->width == pImageOut->width) && (pImageIn->height == pImageOut->height))
	{
		CopyImage(pImageIn, pImageOut);
		return TRUE;
	}

	// Setup variables to increment chroma planes
	int xinc = 1, yinc = 1;
	switch (pImageIn->colorSpace)
	{
	case YUV420:
		xinc = 2;
		yinc = 2;
		break;
	case YUV422:
		xinc = 2;
		break;
	default:
		break;
	}

	// Create temp image buffer for initial h acaling
	IMAGE imageTmp = CreateImage(pImageIn->colorSpace, pImageOut->width, pImageIn->height, DOUBLE);  // Temp image buffer

	// Horizontal scaling
	// Create storage for precomputed pixel contribution tables
	ContribTable contribs, contribsUV;
	if (!MakeContribTable(&contribs, pImageIn->width, pImageOut->width, edgeMethod))
		return FALSE;
	if (pImageIn->colorSpace == YUV420 || pImageIn->colorSpace == YUV422)
	{
		if (!MakeContribTable(&contribsUV, pImageIn->width / 2, pImageOut->width / 2, edgeMethod))
			return FALSE;
	}
	else
	{
		contribsUV.contribPixPos = contribs.contribPixPos;
		contribsUV.filterWeights = contribs.filterWeights;
		contribsUV.numContribPixels = contribs.numContribPixels;
		contribsUV.weightsSum = contribs.weightsSum;
	}

	// Filter image
	// Y/R plane
	for (int y = 0; y < pImageIn->height; y++)
	{
		for (int x = 0; x < pImageOut->width; x++)
		{
			Filter1DHorz(pImageIn, &imageTmp, x, y, Y_PLANE, edgeMethod, contribs);
		}
	}
	// UV/GB planes
	int UVwidth = pImageOut->width / xinc;
	int UVheight = pImageIn->height / yinc;
	for (int plane = U_PLANE; plane <= V_PLANE; plane++)
	{
		for (int y = 0; y < UVheight; y++)
		{
			for (int x = 0; x < UVwidth; x++)
			{
				Filter1DHorz(pImageIn, &imageTmp, x, y, plane, edgeMethod, contribsUV);
			}
		}
	}
	DestroyContribTable(&contribs);
	if (pImageIn->colorSpace == YUV420 || pImageIn->colorSpace == YUV422)
		DestroyContribTable(&contribsUV);

	// Vertical scaling
	// In, out image same size: no rescaling
	if (pImageIn->height == pImageOut->height)
	{
		CopyImage(&imageTmp, pImageOut);
		return TRUE;
	}
	// Create storage for precomputed pixel contribution tables
	if (!MakeContribTable(&contribs, pImageIn->height, pImageOut->height, edgeMethod))
		return FALSE;
	if (pImageIn->colorSpace == YUV420)
	{
		if (!MakeContribTable(&contribsUV, pImageIn->height / 2, pImageOut->height / 2, edgeMethod))
			return FALSE;
	}
	else
	{
		contribsUV.contribPixPos = contribs.contribPixPos;
		contribsUV.filterWeights = contribs.filterWeights;
		contribsUV.numContribPixels = contribs.numContribPixels;
		contribsUV.weightsSum = contribs.weightsSum;
	}

	// Filter image
	// Y/R plane
	for (int y = 0; y < pImageOut->height; y++)
	{
		for (int x = 0; x < pImageOut->width; x++)
		{
			Filter1DVert(&imageTmp, pImageOut, x, y, Y_PLANE, edgeMethod, contribs);
		}
	}
	// UV/GB planes
	UVwidth = pImageOut->width / xinc;
	UVheight = pImageOut->height / yinc;
	for (int plane = U_PLANE; plane <= V_PLANE; plane++)
	{
		for (int y = 0; y < UVheight; y++)
		{
			for (int x = 0; x < UVwidth; x++)
			{
				Filter1DVert(&imageTmp, pImageOut, x, y, plane, edgeMethod, contribsUV);
			}
		}
	}
	DestroyContribTable(&contribs);
	if (pImageIn->colorSpace == YUV420)
		DestroyContribTable(&contribsUV);

	DestroyImage(&imageTmp);
	return TRUE;
}

int main(int argc, char *argv[])
{
	// Command line parser
	CmdLineParms parms;

	// Set default parm values
	parms.scaleRatio = 2.0f;
	parms.fileSubtype = YUV420_I420;
	parms.height = 0;
	parms.width = 0;
	parms.edgeMethod = REPEAT;
	parms.gamma = 1.0f;

	if (!ParseCmdLine(argc, argv, &parms))
		exit(EXIT_FAILURE);

	// Copy parameters to file info structure as needed
	ImageFileInfo inFileInfo;
	ImageFileInfo outFileInfo;

	inFileInfo.fileSubtype = outFileInfo.fileSubtype = parms.fileSubtype;
	inFileInfo.filename = parms.inFilename;
	outFileInfo.filename = parms.outFilename;
	inFileInfo.height = parms.height;
	inFileInfo.width = parms.width;

	// Fill in rest of file info structure
	if (!GetFileInfo(&inFileInfo, &outFileInfo))
		return EXIT_FAILURE;

	// Set output dimensions here since we could determine input dims from BMP header in GetFileInfo()
	// TODO: make output H,W parameters to enable arbitrary scaling ratios
	outFileInfo.height = (int)(inFileInfo.height * parms.scaleRatio + 0.5f);
	outFileInfo.width = (int)(inFileInfo.width * parms.scaleRatio + 0.5f);

	// If over/under max/min image dimensions, exit
	if (outFileInfo.height < MIN_HEIGHT || outFileInfo.height > MAX_HEIGHT ||
		outFileInfo.width < MIN_WIDTH || outFileInfo.width > MAX_WIDTH)
	{
		fprintf(stderr, "Min/max image dimension exceeded!\n");
		return EXIT_FAILURE;
	}

	// Allocate input/output image storage
	IMAGE imageIn;
	switch (inFileInfo.fileType)
	{
	case YUV_FILE:
		// Only YUV420 inputs currently supported
		// TODO: Add YUV422 support
		imageIn = CreateImage(YUV420, inFileInfo.width, inFileInfo.height);
		break;
	case BMP_FILE:
		// Allocate image storage
		imageIn = CreateImage(RGB, inFileInfo.width, inFileInfo.height);
		break;
	default:
		fprintf(stderr, "Unsupported file type for input file %s!\n", inFileInfo.filename);
		return EXIT_FAILURE;
	}
	IMAGE imageOut = CreateImage(imageIn.colorSpace, outFileInfo.width, outFileInfo.height);

	// Allocate storage for light linearized (degamma'ed) image
	IMAGE imageInLinear = CreateImage(imageIn.colorSpace, inFileInfo.width, inFileInfo.height, DOUBLE);

	// Allocate storage for light linearized (degamma'ed) image out
	IMAGE imageOutLinear = CreateImage(imageIn.colorSpace, outFileInfo.width, outFileInfo.height, DOUBLE);

	// Create gamma and inverse gamma LUTs
	// Create 8-bit forward LUT
	double fwdGamma[FWD_GAMMA_LUTSIZE];
	for (int i = 0; i < FWD_GAMMA_LUTSIZE; ++i)
		fwdGamma[i] = (double)pow((double)i / (double)PIXMAX, parms.gamma);

	// Create 12-bit reverse LUT to account for higher resolution needed for linear light/nonlinear perception
	PIXEL bwdGamma[BWD_GAMMA_LUTSIZE];
	const double invGamma = 1.0 / parms.gamma;
	for (int i = 0; i < BWD_GAMMA_LUTSIZE; ++i)
		bwdGamma[i] = (PIXEL)(CLAMP((double)PIXMAX * pow((double)i / BWD_GAMMA_LUTSIZE, invGamma) + 0.5f, 0, PIXMAX));

	char fullInFileName[MAX_STRING_LENGTH];
	char fullOutFileName[MAX_STRING_LENGTH];
	for (int i = 0, outFrame = inFileInfo.startFrame; i < inFileInfo.numFrames; i++)
	{
		switch (inFileInfo.fileType)
		{
		case YUV_FILE:
			if (inFileInfo.numFrames > 1)
				sprintf(fullInFileName, "%s%05d.yuv", inFileInfo.baseFileName, inFileInfo.startFrame + i);
			else
				strncpy(fullInFileName, inFileInfo.filename, MAX_STRING_LENGTH - 1);
			for (int j = 0; j < inFileInfo.numSubFrames; j++, outFrame++)
			{
				// Load input image
				if (LoadRawYUVImage(fullInFileName, &imageIn, j, inFileInfo.fileSubtype))
				{
					if (!DegammaImage(&imageIn, &imageInLinear, fwdGamma))
					{
						fprintf(stderr, "Unable to degamma input image!\n");
						MainCleanup(&imageIn, &imageOut, &imageInLinear, &imageOutLinear);
						return EXIT_FAILURE;
					}

					// Process image
					if (!ResizeImage(&imageInLinear, &imageOutLinear, parms.edgeMethod))
					{
						fprintf(stderr, "Unable to resize image!\n");
						MainCleanup(&imageIn, &imageOut, &imageInLinear, &imageOutLinear);
						return EXIT_FAILURE;
					}

					if (!GammaImage(&imageOutLinear, &imageOut, bwdGamma))
					{
						fprintf(stderr, "Unable to gamma correct output image!\n");
						MainCleanup(&imageIn, &imageOut, &imageInLinear, &imageOutLinear);
						return EXIT_FAILURE;
					}

					// Write output image
					switch (outFileInfo.fileType)
					{
					case YUV_FILE:
						if ((inFileInfo.numFrames > 1) || (inFileInfo.numSubFrames > 1))
							sprintf(fullOutFileName, "%s%05d.yuv", outFileInfo.baseFileName, outFrame);
						else
							strncpy(fullOutFileName, outFileInfo.filename, MAX_STRING_LENGTH - 1);
						SaveRawYUVImage(fullOutFileName, &imageOut, outFileInfo.fileSubtype);
						break;
					case BMP_FILE:
						if ((inFileInfo.numFrames > 1) || (inFileInfo.numSubFrames > 1))
							sprintf(fullOutFileName, "%s%05d.bmp", outFileInfo.baseFileName, outFrame);
						else
							strncpy(fullOutFileName, outFileInfo.filename, MAX_STRING_LENGTH - 1);
						SaveBmpImage(fullOutFileName, &imageOut);
						break;
					default:
						fprintf(stderr, "Unsupported file type for output file %s!\n", outFileInfo.filename);
						MainCleanup(&imageIn, &imageOut, &imageInLinear, &imageOutLinear);
						return EXIT_FAILURE;
					}
				}
			}
			break;
		case BMP_FILE:
			// Load input image
			if (inFileInfo.numFrames > 1)
				sprintf(fullInFileName, "%s%05d.bmp", inFileInfo.baseFileName, inFileInfo.startFrame + i);
			else
				strncpy(fullInFileName, inFileInfo.filename, MAX_STRING_LENGTH - 1);
			if (LoadBmpImage(fullInFileName, &imageIn))
			{
				if (!DegammaImage(&imageIn, &imageInLinear, fwdGamma))
				{
					fprintf(stderr, "Unable to degamma input image!\n");
					MainCleanup(&imageIn, &imageOut, &imageInLinear, &imageOutLinear);
					return EXIT_FAILURE;
				}

				// Process image
				if (!ResizeImage(&imageInLinear, &imageOutLinear, parms.edgeMethod))
				{
					fprintf(stderr, "Unable to resize image!\n");
					MainCleanup(&imageIn, &imageOut, &imageInLinear, &imageOutLinear);
					return EXIT_FAILURE;
				}

				if (!GammaImage(&imageOutLinear, &imageOut, bwdGamma))
				{
					fprintf(stderr, "Unable to gamma correct output image!\n");
					MainCleanup(&imageIn, &imageOut, &imageInLinear, &imageOutLinear);
					return EXIT_FAILURE;
				}

				// Write output image
				switch (outFileInfo.fileType)
				{
				case YUV_FILE:
					if (inFileInfo.numFrames > 1)
						sprintf(fullOutFileName, "%s%05d.bmp", outFileInfo.baseFileName, i);
					else
						strncpy(fullOutFileName, outFileInfo.filename, MAX_STRING_LENGTH - 1);
					SaveRawYUVImage(outFileInfo.filename, &imageOut, outFileInfo.fileSubtype);
					break;
				case BMP_FILE:
					if (inFileInfo.numFrames > 1)
						sprintf(fullOutFileName, "%s%05d.bmp", outFileInfo.baseFileName, i);
					else
						strncpy(fullOutFileName, outFileInfo.filename, MAX_STRING_LENGTH - 1);
					SaveBmpImage(outFileInfo.filename, &imageOut);
					break;
				default:
					fprintf(stderr, "Unsupported file type for output file %s!\n", outFileInfo.filename);
					MainCleanup(&imageIn, &imageOut, &imageInLinear, &imageOutLinear);
					return EXIT_FAILURE;
				}
			}
			break;
		default:
			fprintf(stderr, "Unsupported file type for input file %s!\n", inFileInfo.filename);
			MainCleanup(&imageIn, &imageOut, &imageInLinear, &imageOutLinear);
			return EXIT_FAILURE;
		}
	}

	MainCleanup(&imageIn, &imageOut, &imageInLinear, &imageOutLinear);
	return EXIT_SUCCESS;
}

static void MainCleanup(IMAGE *pImageIn, IMAGE *pImageOut, IMAGE *pImageInLinear, IMAGE *pImageOutLinear)
{
	FCLOSEALL();			// In case of a missed open file stream; shouldn't be necessary
	DestroyImage(pImageIn);
	DestroyImage(pImageOut);
	DestroyImage(pImageInLinear);
	DestroyImage(pImageOutLinear);
}