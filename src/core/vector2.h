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

#ifndef __VECTOR_2_H__
#define __VECTOR_2_H__

#include "core/cpp_headers.h"

template <typename T> class vector2;
typedef vector2<float> float2;
typedef vector2<double> double2;
typedef vector2<unsigned int> uint2;
typedef vector2<int> int2;
typedef vector2<short> short2;
typedef vector2<bool> bool2;
typedef vector2<size_t> size2;
typedef vector2<ssize_t> ssize2;

typedef vector2<unsigned int> pnt;
typedef vector2<int> ipnt;
typedef vector2<float> coord;

template <typename T> class A2E_API __attribute__((packed)) vector2 {
public:
	union {
		T x, u;
	};
	union {
		T y, v;
	};
	
	vector2() : x((T)0), y((T)0) {}
	vector2(const vector2<T>& vec2) : x(vec2.x), y(vec2.y) {}
	vector2(const pair<float, float>& vec2) : x(vec2.first), y(vec2.second) {}
	vector2(const T& vx, const T& vy) : x(vx), y(vy) {}
	vector2(const T& f) : x(f), y(f) {}
	template <typename U> vector2(const vector2<U>& vec2) : x((T)vec2.x), y((T)vec2.y) {}
	~vector2() {}
	
	T& operator[](size_t index) const {
		return ((T*)this)[index];
	}
	
	friend ostream& operator<<(ostream& output, const vector2<T>& vec2) {
		output << "(" << vec2.x << ", " << vec2.y << ")";
		return output;
	}
	
	void set(const T& vx, const T& vy) {
		x = vx;
		y = vy;
	}
	void set(const vector2& vec2) {
		x = vec2.x;
		y = vec2.y;
	}
	
	vector2& round();
	vector2& normalize() {
		if(!is_null()) {
			*this = *this / length();
		}
		return *this;
	}

	// TODO: fully integrate ...
	
	friend vector2 operator*(const T& f, const vector2& v) {
		return vector2<T>(f * v.x, f * v.y);
	}
	vector2 operator*(const T& f) const {
		return vector2<T>(f * this->x, f * this->y);
	}
	vector2 operator/(const T& f) const {
		return vector2<T>(this->x / f, this->y / f);
	}
	
	vector2 operator-(const vector2<T>& vec2) const {
		return vector2<T>(this->x - vec2.x, this->y - vec2.y);
	}
	vector2 operator+(const vector2<T>& vec2) const {
		return vector2<T>(this->x + vec2.x, this->y + vec2.y);
	}
	vector2 operator*(const vector2<T>& vec2) const {
		return vector2<T>(this->x * vec2.x, this->y * vec2.y);
	}
	vector2 operator/(const vector2<T>& vec2) const {
		return vector2<T>(this->x / vec2.x, this->y / vec2.y);
	}
	
	vector2& operator+=(const vector2& v) {
		*this = *this + v;
		return *this;
	}
	vector2& operator-=(const vector2& v) {
		*this = *this - v;
		return *this;
	}
	vector2& operator*=(const vector2& v) {
		*this = *this * v;
		return *this;
	}
	vector2& operator/=(const vector2& v) {
		*this = *this / v;
		return *this;
	}
	
	T length() const {
		return sqrt(dot());
	}

	T distance(const vector2<T>& vec2) const {
		return (vec2 - *this).length();
	}
	
	T dot(const vector2<T>& vec2) const {
		return x*vec2.x + y*vec2.y;
	}
	T dot() const {
		return dot(*this);
	}
	
	bool is_null() const;
	bool is_nan() const;
	bool is_inf() const;
	
	vector2<T>& clamp(const T& vmin, const T& vmax) {
		x = (x < vmin ? vmin : (x > vmax ? vmax : x));
		y = (y < vmin ? vmin : (y > vmax ? vmax : y));
		return *this;
	}
	vector2<T>& clamp(const vector2<T>& vmin, const vector2<T>& vmax) {
		x = (x < vmin.x ? vmin.x : (x > vmax.x ? vmax.x : x));
		y = (y < vmin.y ? vmin.y : (y > vmax.y ? vmax.y : y));
		return *this;
	}
	vector2<T> sign() const {
		return vector2<T>(x < 0.0f ? -1.0f : 1.0f, y < 0.0f ? -1.0f : 1.0f);
	}
	
	// a component-wise minimum between two vector2s
	static const vector2 min(const vector2& v1, const vector2& v2) {
		return vector2(std::min(v1.x, v2.x), std::min(v1.y, v2.y));
	}
	
	// a component-wise maximum between two vector2s
	static const vector2 max(const vector2& v1, const vector2& v2) {
		return vector2(std::max(v1.x, v2.x), std::max(v1.y, v2.y));
	}
	
};

template<> vector2<float>& vector2<float>::round();
template<typename T> vector2<T>& vector2<T>::round() {
	x = ::round(x);
	y = ::round(y);
	return *this;
}

template<typename T> bool vector2<T>::is_null() const {
	return (this->x == (T)0 && this->y == (T)0 ? true : false);
}

template<typename T> bool vector2<T>::is_nan() const {
	if(!numeric_limits<T>::has_quiet_NaN) return false;
	
	T nan = numeric_limits<T>::quiet_NaN();
	if(x == nan || y == nan) {
		return true;
	}
	return false;
}

template<typename T> bool vector2<T>::is_inf() const {
	if(!numeric_limits<T>::has_infinity) return false;
	
	T inf = numeric_limits<T>::infinity();
	if(x == inf || x == -inf || y == inf || y == -inf) {
		return true;
	}
	return false;
}

#if defined(A2E_EXPORT)
// only instantiate this in the vector2.cpp
extern template class vector2<float>;
extern template class vector2<double>;
extern template class vector2<unsigned int>;
extern template class vector2<int>;
extern template class vector2<short>;
extern template class vector2<bool>;
extern template class vector2<size_t>;
extern template class vector2<ssize_t>;
#endif

#endif