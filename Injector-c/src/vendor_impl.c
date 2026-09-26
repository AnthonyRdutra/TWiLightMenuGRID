/* Single translation unit that instantiates the vendored stb_image header-only libraries'
 * implementation (their own convention: #define ..._IMPLEMENTATION before including in exactly
 * one .c file). The webview library needs the same treatment but requires a C++ compiler for its
 * implementation -- see src/webview_impl.cpp. */
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
