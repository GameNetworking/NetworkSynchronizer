/*************************************************************************/
/*  test_bit_array.h                                                     */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2021 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2021 Godot Engine contributors (cf. AUTHORS.md).   */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#ifndef TEST_NETSYNC_BIT_ARRAY_H
#define TEST_NETSYNC_BIT_ARRAY_H

#include "../bit_array.h"

#include "tests/test_macros.h"
#include <limits>

namespace test_netsync_BitArray {

TEST_CASE("[NetSync][BitArray] Read and write") {
	BitArray array;
	int offset = 0;
	int bits = {};
	std::uint64_t value = {};

	SUBCASE("[NetSync][BitArray] One bit") {
		bits = 1;

		SUBCASE("[NetSync][BitArray] One") {
			value = 0b1;
		}
		SUBCASE("[NetSync][BitArray] Zero") {
			value = 0b0;
		}
	}
	SUBCASE("[NetSync][BitArray] 16 mixed bits") {
		bits = 16;
		value = 0b1010101010101010;
	}
	SUBCASE("[NetSync][BitArray] One and 4 zeroes") {
		bits = 5;
		value = 0b10000;
	}
	SUBCASE("[NetSync][BitArray] 64 bits") {
		bits = 64;

		SUBCASE("[NetSync][BitArray] One") {
			value = UINT64_MAX;
		}
		SUBCASE("[NetSync][BitArray] Zero") {
			value = 0;
		}
	}
	SUBCASE("[NetSync][BitArray] One bit with offset") {
		bits = 1;
		offset = 64;
		array.resize_in_bits(offset);

		SUBCASE("[NetSync][BitArray] One") {
			array.store_bits(0, UINT64_MAX, 64);
			value = 0b0;
		}
		SUBCASE("[NetSync][BitArray] Zero") {
			array.store_bits(0, 0, 64);
			value = 0b1;
		}
	}

	array.resize_in_bits(offset + bits);
	array.store_bits(offset, value, bits);
	std::uint64_t buffer_val = 0;
	CHECK_MESSAGE(array.read_bits(offset, bits, buffer_val), "Reading failed.");
	CHECK_MESSAGE((buffer_val == value), "Should read the same value");
}

TEST_CASE("[NetSync][BitArray] Constructing from Vector") {
	Vector<std::uint8_t> data;
	data.push_back(-1);
	data.push_back(0);
	data.push_back(1);

	const BitArray array(data);
	CHECK_MESSAGE(array.size_in_bits() == data.size() * 8.0, "Number of bits must be equal to size of original data");
	CHECK_MESSAGE(array.size_in_bytes() == data.size(), "Number of bytes must be equal to size of original data");
	for (int i = 0; i < data.size(); ++i) {
		std::uint64_t buffer_val = 0;
		CHECK_MESSAGE(array.read_bits(i * 8, 8, buffer_val), "Reading should never fail.");
		CHECK_MESSAGE(std::uint8_t(buffer_val) == data[i], "Readed bits should be equal to the original");
	}
}

TEST_CASE("[NetSync][BitArray] Pre-allocation and zeroing") {
	constexpr std::uint64_t value = std::numeric_limits<std::uint64_t>::max();
	constexpr int bits = sizeof(value);

	BitArray array(bits);
	CHECK_MESSAGE(array.size_in_bits() == bits, "Number of bits must be equal to allocated");
	array.store_bits(0, value, bits);
	array.zero();
	std::uint64_t buffer_val = 0;
	CHECK_MESSAGE(array.read_bits(0, bits, buffer_val), "Reading should never fail.");
	CHECK_MESSAGE(buffer_val == 0, "Should read zero");
}
} //namespace test_netsync_BitArray

#endif
