/* Copyright 2008 Cedric Cellier.
 *
 * This file is part of Scambio.
 *
 * Scambio is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Scambio is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with Scambio.  If not, see <http://www.gnu.org/licenses/>.
 */
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
