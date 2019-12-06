#include "special_functionality.h"

const bool gTKOEnableSpecialFunctionality =
#ifdef TKO_SPECIAL_FUNCTIONALITY
	true;
#else
	false;
#endif