#ifndef _TEST_WM_H_
#define _TEST_WM_H_

#include <stdlib.h>
#include "wm-log.h"

/*
// #define assert(EXPRESSION) \
// 	if (!(EXPRESSION)) { \
// 		LOG_FATAL("ASSERT failed at line %d in file %s: %s", __LINE__, __FILE__, #EXPRESSION); \
// 	} else { LOG_DEBUG(". %s", #EXPRESSION); }
*/

#define assert(EXPRESSION) \
	if (!(EXPRESSION)) { \
		LOG_CLEAN("%s:%d: %s - FAIL", __FILE__, __LINE__, #EXPRESSION); \
		exit(1); \
	} \
	else { LOG_CLEAN("%s:%d: %s - pass", __FILE__, __LINE__, #EXPRESSION); }


#endif
