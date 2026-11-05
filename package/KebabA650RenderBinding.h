/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_A650_RENDER_BINDING_H
#define KEBAB_A650_RENDER_BINDING_H
#include <stdint.h>

enum {KebabA650RenderNativeQualified = 1};
static const uint8_t KebabA650RenderSourceContract[32] = {0xc2, 0x54, 0x45, 0xef, 0x60, 0x68, 0x21, 0x33, 0x9b, 0xe3, 0x9e, 0x22, 0x5d, 0x98, 0x59, 0x64, 0x47, 0x03, 0xc3, 0x96, 0xff, 0xe5, 0x97, 0x04, 0x8b, 0x25, 0xa7, 0x47, 0xc2, 0x95, 0x28, 0x2a};

static inline int
KARSourceBound(void)
{
    unsigned any = 0;
    for (unsigned i = 0; i < sizeof KebabA650RenderSourceContract; ++i) {
        any |= KebabA650RenderSourceContract[i];
    }
    return KebabA650RenderNativeQualified == 1 && any;
}
#endif
