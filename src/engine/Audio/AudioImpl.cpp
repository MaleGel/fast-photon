// miniaudio implementation unit with OGG Vorbis support via stb_vorbis.
//
// miniaudio has built-in Vorbis wiring — it checks for the macro
// `STB_VORBIS_INCLUDE_STB_VORBIS_H` and, when set, uses stb_vorbis types
// directly from its own code. We arrange the include order as follows:
//
//   1) Include stb_vorbis.c in "header-only" mode to get its declarations
//      without the implementation (avoids duplicate symbols later).
//   2) Define STB_VORBIS_INCLUDE_STB_VORBIS_H manually so miniaudio's
//      built-in Vorbis support turns on.
//   3) Include miniaudio with its IMPLEMENTATION macro.
//   4) Undefine stb_vorbis's short single-letter macros (L/R/C/M) — they
//      clash with winnt.h field names used by code that transitively
//      includes <windows.h>.
//   5) Include stb_vorbis.c again WITHOUT the header-only guard to bring
//      in the implementation.

// Step 1 — declarations only.
#define STB_VORBIS_HEADER_ONLY
extern "C" {
#include <stb_vorbis.c>
}
#undef STB_VORBIS_HEADER_ONLY

// Step 2 — tell miniaudio we already have stb_vorbis in this translation unit.
#define STB_VORBIS_INCLUDE_STB_VORBIS_H

// Step 3 — miniaudio implementation.
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

// stb_vorbis pollutes the preprocessor with short single-letter macros near
// the bottom of its .c file; miniaudio.h (step 3) has already pulled in
// <windows.h>, but we still need to keep things clean for anyone later.
// Re-including the .c file for its implementation would redefine L/R/C/M,
// so the cleanest fix is to keep them after this TU and never include the
// file again elsewhere.

// Step 5 — full stb_vorbis implementation.
extern "C" {
#include <stb_vorbis.c>
}

// Step 4 — scrub the polluting single-letter macros so nothing downstream
// in this TU picks them up accidentally.
#undef L
#undef C
#undef R
#undef M
