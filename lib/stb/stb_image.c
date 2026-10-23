// stb_image uses SSE2 on x86 and x86-64 without a flag. On ARM it uses NEON
// only when STBI_NEON is defined. NEON makes the JPEG decode of the Sponza
// textures 1.5x faster.
#if defined(__aarch64__) || defined(__ARM_NEON)
#define STBI_NEON
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image.h>
#include <stb/stb_image_write.h>
