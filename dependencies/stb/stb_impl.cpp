// Single translation unit that provides the stb_image and stb_image_resize2
// implementations.  All other TUs include the headers without the _IMPLEMENTATION
// define.

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>
