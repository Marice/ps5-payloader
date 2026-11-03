// ps5-homebrew-ui - The single compilation unit for the vendored stb_image.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Only PNG is decoded, from memory: images are read into memory first, so stdio is left out.

#define STB_IMAGE_IMPLEMENTATION 1
#define STBI_ONLY_PNG 1
#define STBI_NO_STDIO 1
#include "stb_image.inc"
