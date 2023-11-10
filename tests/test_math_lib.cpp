#pragma once

#include "test_math_lib.h"
#include "modules/network_synchronizer/tests/test_math_lib.h"
#include <cmath>

namespace NS_Test {

Vec3::operator NS::VarData() const {
	NS::VarData vd;
	vd.data.vec.x = x;
	vd.data.vec.y = y;
	vd.data.vec.z = z;
	return vd;
}

Vec3 Vec3::from(const NS::VarData &p_vd) {
	Vec3 v;
	v.x = p_vd.data.vec.x;
	v.y = p_vd.data.vec.y;
	v.z = p_vd.data.vec.z;
	return v;
}

Vec3 Vec3::operator+(const Vec3 &p_vd) const {
	Vec3 v = *this;
	v += p_vd;
	return v;
}

Vec3 &Vec3::operator+=(const Vec3 &p_vd) {
	x += p_vd.x;
	y += p_vd.y;
	z += p_vd.z;
	return *this;
}

Vec3 Vec3::operator-(const Vec3 &p_vd) const {
	Vec3 v = *this;
	v -= p_vd;
	return v;
}

Vec3 &Vec3::operator-=(const Vec3 &p_vd) {
	x -= p_vd.x;
	y -= p_vd.y;
	z -= p_vd.z;
	return *this;
}

Vec3 Vec3::operator/(const Vec3 &p_vd) const {
	Vec3 v = *this;
	v /= p_vd;
	return v;
}

Vec3 &Vec3::operator/=(const Vec3 &p_vd) {
	x /= p_vd.x;
	y /= p_vd.y;
	z /= p_vd.z;
	return *this;
}

Vec3 Vec3::operator*(const Vec3 &p_vd) const {
	Vec3 v = *this;
	v *= p_vd;
	return v;
}

Vec3 &Vec3::operator*=(const Vec3 &p_vd) {
	x *= p_vd.x;
	y *= p_vd.y;
	z *= p_vd.z;
	return *this;
}

Vec3 Vec3::operator/(const float p_val) const {
	Vec3 v = *this;
	v /= p_val;
	return v;
}

Vec3 &Vec3::operator/=(const float &p_val) {
	x /= p_val;
	y /= p_val;
	z /= p_val;
	return *this;
}

Vec3 Vec3::operator*(const float p_val) const {
	Vec3 v = *this;
	v *= p_val;
	return v;
}

Vec3 &Vec3::operator*=(const float &p_val) {
	x *= p_val;
	y *= p_val;
	z *= p_val;
	return *this;
}

float Vec3::length() const {
	return std::sqrt(x * x + y * y + z * z);
}

void Vec3::normalize() {
	float l = length();
	if (l > 0.0001) {
		*this /= l;
	} else {
		x = 0.0;
		y = 0.0;
		z = 0.0;
	}
}

Vec3 Vec3::normalized() const {
	Vec3 v = *this;
	v.normalize();
	return v;
}

float Vec3::distance_to(const Vec3 &p_v) const {
	Vec3 v = *this;
	v -= p_v;
	return v.length();
}

}; //namespace NS_Test
