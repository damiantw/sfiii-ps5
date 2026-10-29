// Force-included first in every console compile (-include). FreeBSD's <sys/types.h> defines
// major()/minor() as macros, which breaks ordinary functions of those names (the runtime's VDP1
// raster has a major()). Include it once here and drop the macros. MIT, see LICENSE.
#pragma once
#include <sys/types.h>
#undef major
#undef minor
#undef makedev
