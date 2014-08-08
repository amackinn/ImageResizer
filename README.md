#ImageResizer

Scale images up and down

This is a command line utility to rescale images written in C. Includes MSVS2013 solution file for build on Windows. Also supports *nix OSes.

Supports BMP and raw YUV image file formats. 
Image formats supported:
* BMP
* YUV420_I420
* YUV420_YV12
* YUV420_NV12
* YUV420_NV21

##Known issues

1. Utility currently supports only upscale 2x and downscale 1/2x via command line parameters. The program itself supports arbitrary rescale ratios, but this is untested.

