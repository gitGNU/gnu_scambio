#ifndef MISCMAC_H_080416
#define MISCMAC_H_080416

#define sizeof_array(x) (sizeof(x)/sizeof(*(x)))

#define COMPILE_ASSERT(x) switch(x) { \
	case 0: break; \
	case (x): break; \
}

#define TOSTRX(x) #x
#define TOSTR(x) TOSTRX(x)

#include <stddef.h>
#define DOWNCAST(val, member, subtype) \
	((struct subtype *)((char *)val - offsetof(struct subtype, member)))

#endif
