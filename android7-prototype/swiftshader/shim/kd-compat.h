/* Kindleroid: API 24 bionic headers with a newer clang. bionic N attaches __warnattr to a second declaration
   of sprintf/vsprintf, which clang 14 rejects ("'warning' attribute does not appear on the first declaration").
   The attribute is only a diagnostic; drop it. Force-included before every source file. */
#include <sys/cdefs.h>
#undef __warnattr
#define __warnattr(msg)
