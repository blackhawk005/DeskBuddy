// ============================================================================
// version.h  —  firmware build identity
//
// FW_VERSION identifies the running build. CI overrides it on the compile line
// with the git short SHA:
//     --build-property "build.extra_flags=-DFW_VERSION=\"<sha>\""
// so every push produces a distinct version. Local/USB builds fall back to
// "dev", which never equals a CI SHA, so a locally flashed device will always
// see the first CI release as "newer" and adopt it.
// ============================================================================
#pragma once

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif
