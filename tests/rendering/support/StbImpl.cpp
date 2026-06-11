// StbImpl.cpp — single translation unit that defines stb_image and
// stb_image_write implementations for the rendering test targets.
//
// engine_tools already defines STB_IMAGE_IMPLEMENTATION in its own
// StbImageImpl.cpp, but rendering_tests does NOT link engine_tools,
// so we need our own definition here.  The two definitions live in
// separate static libraries and are never merged, so there is no ODR
// violation.

#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
