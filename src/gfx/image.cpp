// PayLoader - A texture from a PNG file, decoded by the vendored stb_image.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gfx/image.hpp"

#include "core/save_file.hpp"

extern "C"
{
#include "third_party/stb/image.h"
}

#include <GL/glcorearb.h>

#include <string>

namespace hui::gfx
{

std::uint32_t load_png_texture(const std::string &path)
{
    // Read with the kit's reader, not std::ifstream: libc++'s streams pull in locale functions
    // (strtod_l and others) that the console's libc lacks.
    std::string png;
    if (!save::read_file(path, &png) || png.empty())
        return 0;
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char *rgba =
        stbi_load_from_memory(reinterpret_cast<const unsigned char *>(png.data()),
                              static_cast<int>(png.size()), &width, &height, &channels, 4);
    if (rgba == nullptr)
        return 0;
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    // One level only: mip chains leave ps5-opengl's batched fast path.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    stbi_image_free(rgba);
    return texture;
}

} // namespace hui::gfx
