/*
 *  Albion 2 Engine "light"
 *  Copyright (C) 2004 - 2012 Florian Ziesche
 *  
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License only.
 *  
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "vector2.h"

template<> A2E_API float2& vector2<float>::round() {
	x = roundf(x);
	y = roundf(y);
	return *this;
}

#if defined(A2E_EXPORT)
// instantiate
template class vector2<float>;
template class vector2<double>;
template class vector2<unsigned int>;
template class vector2<int>;
template class vector2<short>;
template class vector2<bool>;
template class vector2<size_t>;
template class vector2<ssize_t>;
#endif
