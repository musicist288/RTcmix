/* RTcmix  - Copyright (C) 2000  The RTcmix Development Team
   See ``AUTHORS'' for a list of contributors. See ``LICENSE'' for
   the license to this software and for a DISCLAIMER OF ALL WARRANTIES.
*/
#include <prototypes.h>
#include "../sys/mixerr.h"


extern "C" {
	double rtdispatch(char *fname, double *pp, int n_args)
	{
		double rv;

		rv = checkInsts(fname, pp, n_args);
		return rv;
	}
}


