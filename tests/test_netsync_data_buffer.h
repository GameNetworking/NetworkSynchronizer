#ifndef TEST_NETSYNC_DATA_BUFFER_H
#define TEST_NETSYNC_DATA_BUFFER_H

#include "../godot4/gd_network_interface.h"
#include "../scene_synchronizer.h"
#include "data_buffer.h"

#include "modules/network_synchronizer/godot4/gd_network_interface.h"
#include "modules/network_synchronizer/godot4/gd_scene_synchronizer.h"
#include "tests/test_macros.h"

namespace test_netsync_NS::DataBuffer {

inline Vector<double> real_values(NS::DataBuffer::CompressionLevel p_compression_level) {
	Vector<double> values;
	values.append(Math_PI);
	values.append(0.0);
	values.append(-3.04);
	values.append(3.04);
	values.append(0.5);
	values.append(-0.5);
	values.append(1);
	values.append(-1);
	values.append(0.9);
	values.append(-0.9);
	values.append(3.9);
	values.append(-3.9);
	values.append(8);

	switch (p_compression_level) {
		case NS::DataBuffer::COMPRESSION_LEVEL_3: {
			values.append(-15'360);
			values.append(15'360);
		} break;
		case NS::DataBuffer::COMPRESSION_LEVEL_2: {
			// https://en.wikipedia.org/wiki/Half-precision_floating-point_format#Half_precision_examples
			values.append(-65'504);
			values.append(65'504);
			values.append(Math::pow(2.0, -14) / 1024);
			values.append(Math::pow(2.0, -14) * 1023 / 1024);
			values.append(Math::pow(2.0, -1) * (1 + 1023.0 / 1024));
			values.append((1 + 1.0 / 1024));
		} break;
		case NS::DataBuffer::COMPRESSION_LEVEL_1: {
			// https://en.wikipedia.org/wiki/Single-precision_floating-point_format#Single-precision_examples
			values.append(FLT_MIN);
			values.append(-FLT_MAX);
			values.append(FLT_MAX);
			values.append(Math::pow(2.0, -149));
			values.append(Math::pow(2.0, -126) * (1 - Math::pow(2.0, -23)));
			values.append(1 - Math::pow(2.0, -24));
			values.append(1 + Math::pow(2.0, -23));
		} break;
		case NS::DataBuffer::COMPRESSION_LEVEL_0: {
			// https://en.wikipedia.org/wiki/Double-precision_floating-point_format#Double-precision_examples
			values.append(DBL_MIN);
			values.append(DBL_MAX);
			values.append(-DBL_MAX);
			values.append(1.0000000000000002);
			values.append(4.9406564584124654 * Math::pow(10.0, -324));
			values.append(2.2250738585072009 * Math::pow(10.0, -308));
		} break;
	}

	return values;
}

TEST_CASE("[NetSync][NS::DataBuffer] Bool") {
	bool value = {};

	SUBCASE("[NetSync][NS::DataBuffer] false") {
		value = false;
	}
	SUBCASE("[NetSync][NS::DataBuffer] true") {
		value = true;
	}

	NS::DataBuffer buffer;
	buffer.begin_write(0);
	CHECK_MESSAGE(buffer.add_bool(value) == value, "Should return the same value");

	CHECK(buffer.get_bit_offset() == buffer.get_bit_taken(NS::DataBuffer::DATA_TYPE_BOOL, NS::DataBuffer::COMPRESSION_LEVEL_0));

	buffer.begin_read();
	CHECK_MESSAGE(buffer.read_bool() == value, "Should read the same value");
}

TEST_CASE("[NetSync][NS::DataBuffer] Int") {
	NS::DataBuffer::CompressionLevel compression_level = {};
	int64_t value = {};

	NS::DataBuffer buffer;
	SUBCASE("[NetSync][NS::DataBuffer] Compression level 3") {
		compression_level = NS::DataBuffer::COMPRESSION_LEVEL_3;

		SUBCASE("[NetSync][NS::DataBuffer] Positive") {
			value = 127;
		}
		SUBCASE("[NetSync][NS::DataBuffer] Zero") {
			value = 0;
		}
		SUBCASE("[NetSync][NS::DataBuffer] Negative") {
			value = -128;
		}
	}

	SUBCASE("[NetSync][NS::DataBuffer] Compression level 2") {
		compression_level = NS::DataBuffer::COMPRESSION_LEVEL_2;

		SUBCASE("[NetSync][NS::DataBuffer] Positive") {
			value = 32767;
		}
		SUBCASE("[NetSync][NS::DataBuffer] Zero") {
			value = 0;
		}
		SUBCASE("[NetSync][NS::DataBuffer] Negative") {
			value = -32768;
		}
	}

	SUBCASE("[NetSync][NS::DataBuffer] Compression level 1") {
		compression_level = NS::DataBuffer::COMPRESSION_LEVEL_1;

		SUBCASE("[NetSync][NS::DataBuffer] Positive") {
			value = 2147483647;
		}
		SUBCASE("[NetSync][NS::DataBuffer] Zero") {
			value = 0;
		}
		SUBCASE("[NetSync][NS::DataBuffer] Negative") {
			value = -2147483648LL;
		}
	}

	SUBCASE("[NetSync][NS::DataBuffer] Compression level 0") {
		compression_level = NS::DataBuffer::COMPRESSION_LEVEL_0;

		SUBCASE("[NetSync][NS::DataBuffer] Positive") {
			value = 2147483647;
		}
		SUBCASE("[NetSync][NS::DataBuffer] Zero") {
			value = 0;
		}
		SUBCASE("[NetSync][NS::DataBuffer] Negative") {
			value = -9223372036854775807LL;
		}
	}

	buffer.begin_write(0);
	CHECK_MESSAGE(buffer.add_int(value, compression_level) == value, "Should return the same value");

	CHECK(buffer.get_bit_offset() == buffer.get_bit_taken(NS::DataBuffer::DATA_TYPE_INT, compression_level));

	buffer.begin_read();
	CHECK_MESSAGE(buffer.read_int(compression_level) == value, "Should read the same value");
}

TEST_CASE("[NetSync][NS::DataBuffer] Uint") {
	NS::DataBuffer::CompressionLevel compression_level = {};
	int64_t value = {};

	NS::DataBuffer buffer;
	SUBCASE("[NetSync][NS::DataBuffer] Uint Compression level 3") {
		compression_level = NS::DataBuffer::COMPRESSION_LEVEL_3;

		SUBCASE("[NetSync][NS::DataBuffer] Uint CLevel 3 Positive") {
			value = UINT8_MAX;
		}
		SUBCASE("[NetSync][NS::DataBuffer] Uint CLevel 3 Zero") {
			value = 0;
		}
	}

	SUBCASE("[NetSync][NS::DataBuffer] Compression level 2") {
		compression_level = NS::DataBuffer::COMPRESSION_LEVEL_2;

		SUBCASE("[NetSync][NS::DataBuffer] Positive") {
			value = UINT16_MAX;
		}
		SUBCASE("[NetSync][NS::DataBuffer] Zero") {
			value = 0;
		}
	}

	SUBCASE("[NetSync][NS::DataBuffer] Compression level 1") {
		compression_level = NS::DataBuffer::COMPRESSION_LEVEL_1;

		SUBCASE("[NetSync][NS::DataBuffer] Positive") {
			value = UINT32_MAX;
		}
		SUBCASE("[NetSync][NS::DataBuffer] Zero") {
			value = 0;
		}
	}

	SUBCASE("[NetSync][NS::DataBuffer] Compression level 0") {
		compression_level = NS::DataBuffer::COMPRESSION_LEVEL_0;

		SUBCASE("[NetSync][NS::DataBuffer] Positive") {
			value = UINT64_MAX;
		}
		SUBCASE("[NetSync][NS::DataBuffer] Zero") {
			value = 0;
		}
	}

	buffer.begin_write(0);
	CHECK_MESSAGE(buffer.add_uint(value, compression_level) == value, "Should return the same value");

	CHECK(buffer.get_bit_offset() == buffer.get_bit_taken(NS::DataBuffer::DATA_TYPE_UINT, compression_level));

	buffer.begin_read();
	CHECK_MESSAGE(buffer.read_uint(compression_level) == value, "Should read the same value");
}

TEST_CASE("[NetSync][NS::DataBuffer] Real") {
	NS::DataBuffer::CompressionLevel compression_level = {};

	SUBCASE("[NetSync][NS::DataBuffer] Compression level 3 (Minifloat)") {
		compression_level = NS::DataBuffer::COMPRESSION_LEVEL_3;
	}

	SUBCASE("[NetSync][NS::DataBuffer] Compression level 2 (Half perception)") {
		compression_level = NS::DataBuffer::COMPRESSION_LEVEL_2;
	}

	SUBCASE("[NetSync][NS::DataBuffer] Compression level 1 (Single perception)") {
		compression_level = NS::DataBuffer::COMPRESSION_LEVEL_1;
	}

	SUBCASE("[NetSync][NS::DataBuffer] Compression level 0 (Double perception)") {
		compression_level = NS::DataBuffer::COMPRESSION_LEVEL_0;
	}

	const Vector<double> values = real_values(compression_level);
	const double epsilon = Math::pow(2.0, NS::DataBuffer::get_mantissa_bits(compression_level) - 1);
	for (int i = 0; i < values.size(); ++i) {
		NS::DataBuffer buffer;

		buffer.begin_write(0);
		const double value = values[i];
		CHECK_MESSAGE(buffer.add_real(value, compression_level) == doctest::Approx(value).epsilon(epsilon), "Should return the same value");

		CHECK(buffer.get_bit_offset() == buffer.get_bit_taken(NS::DataBuffer::DATA_TYPE_REAL, compression_level));

		buffer.begin_read();
		CHECK_MESSAGE(buffer.read_real(compression_level) == doctest::Approx(value).epsilon(epsilon), "Should read the same value");
	}
}

TEST_CASE("[NetSync][NS::DataBuffer] Positive unit real") {
	NS::DataBuffer::CompressionLevel compression_level = {};
	double epsilon = {};

	SUBCASE("[NetSync][NS::DataBuffer] Compression level 3") {
		compression_level = NS::DataBuffer::COMPRESSION_LEVEL_3;
		epsilon = 0.033335;
	}

	SUBCASE("[NetSync][NS::DataBuffer] Compression level 2") {
		compression_level = NS::DataBuffer::COMPRESSION_LEVEL_2;
		epsilon = 0.007935;
	}

	SUBCASE("[NetSync][NS::DataBuffer] Compression level 1") {
		compression_level = NS::DataBuffer::COMPRESSION_LEVEL_1;
		epsilon = 0.00196;
	}

	SUBCASE("[NetSync][NS::DataBuffer] Compression level 0") {
		compression_level = NS::DataBuffer::COMPRESSION_LEVEL_0;
		epsilon = 0.00049;
	}

	const Vector<double> values = real_values(compression_level);
	for (int i = 0; i < values.size(); ++i) {
		NS::DataBuffer buffer;

		const double value = values[i];
		if (value < 0) {
			// Skip negative values
			continue;
		}

		double value_integral;
		const double value_unit = modf(values[i], &value_integral);
		buffer.begin_write(0);
		CHECK_MESSAGE(buffer.add_positive_unit_real(value_unit, compression_level) == doctest::Approx(value_unit).epsilon(epsilon), "Should return the same value");

		CHECK(buffer.get_bit_offset() == buffer.get_bit_taken(NS::DataBuffer::DATA_TYPE_POSITIVE_UNIT_REAL, compression_level));

		buffer.begin_read();
		CHECK_MESSAGE(buffer.read_positive_unit_real(compression_level) == doctest::Approx(value_unit).epsilon(epsilon), "Should read the same value");
	}
}

TEST_CASE("[NetSync][NS::DataBuffer] Unit real") {
	NS::DataBuffer::CompressionLevel compression_level = {};
	double epsilon = {};

	SUBCASE("[NetSync][NS::DataBuffer] Compression level 3") {
		compression_level = NS::DataBuffer::COMPRESSION_LEVEL_3;
		epsilon = 0.033335;
	}

	SUBCASE("[NetSync][NS::DataBuffer] Compression level 2") {
		compression_level = NS::DataBuffer::COMPRESSION_LEVEL_2;
		epsilon = 0.007935;
	}

	SUBCASE("[NetSync][NS::DataBuffer] Compression level 1") {
		compression_level = NS::DataBuffer::COMPRESSION_LEVEL_1;
		epsilon = 0.00196;
	}

	SUBCASE("[NetSync][NS::DataBuffer] Compression level 0") {
		compression_level = NS::DataBuffer::COMPRESSION_LEVEL_0;
		epsilon = 0.00049;
	}

	const Vector<double> values = real_values(compression_level);
	for (int i = 0; i < values.size(); ++i) {
		NS::DataBuffer buffer;

		double value_integral;
		const double value_unit = modf(values[i], &value_integral);
		buffer.begin_write(0);
		CHECK_MESSAGE(buffer.add_unit_real(value_unit, compression_level) == doctest::Approx(value_unit).epsilon(epsilon), "Should return the same value");

		CHECK(buffer.get_bit_offset() == buffer.get_bit_taken(NS::DataBuffer::DATA_TYPE_UNIT_REAL, compression_level));

		buffer.begin_read();
		CHECK_MESSAGE(buffer.read_unit_real(compression_level) == doctest::Approx(value_unit).epsilon(epsilon), "Should read the same value");
	}
}

TEST_CASE("[NetSync][NS::DataBuffer] Vector2") {
	NS::DataBuffer::CompressionLevel compression_level = {};

	SUBCASE("[NetSync][NS::DataBuffer] Compression level 3") {
		compression_level = NS::DataBuffer::COMPRESSION_LEVEL_3;
	}

	SUBCASE("[NetSync][NS::DataBuffer] Compression level 2") {
		compression_level = NS::DataBuffer::COMPRESSION_LEVEL_2;
	}

	SUBCASE("[NetSync][NS::DataBuffer] Compression level 1") {
		compression_level = NS::DataBuffer::COMPRESSION_LEVEL_1;
	}

	SUBCASE("[NetSync][NS::DataBuffer] Compression level 0") {
		compression_level = NS::DataBuffer::COMPRESSION_LEVEL_0;
	}

	const double epsilon = Math::pow(2.0, NS::DataBuffer::get_mantissa_bits(compression_level) - 1);
	const Vector<double> values = real_values(compression_level);
	for (int i = 0; i < values.size(); ++i) {
		NS::DataBuffer buffer;

#ifdef REAL_T_IS_DOUBLE
		const Vector2 value = Vector2(values[i], values[i]);
#else
		const real_t clamped_value = CLAMP(values[i], -FLT_MIN, FLT_MAX);
		const Vector2 value = Vector2(clamped_value, clamped_value);
#endif
		buffer.begin_write(0);
		const Vector2 added_value = buffer.add_vector2(value, compression_level);
		CHECK_MESSAGE(added_value.x == doctest::Approx(value.x).epsilon(epsilon), "Added Vector2 should have the same x axis");
		CHECK_MESSAGE(added_value.y == doctest::Approx(value.y).epsilon(epsilon), "Added Vector2 should have the same y axis");

		CHECK(buffer.get_bit_offset() == buffer.get_bit_taken(NS::DataBuffer::DATA_TYPE_VECTOR2, compression_level));

		buffer.begin_read();
		const Vector2 read_value = buffer.read_vector2(compression_level);
		CHECK_MESSAGE(read_value.x == doctest::Approx(value.x).epsilon(epsilon), "Read Vector2 should have the same x axis");
		CHECK_MESSAGE(read_value.y == doctest::Approx(value.y).epsilon(epsilon), "Read Vector2 should have the same y axis");
	}
}

TEST_CASE("[NetSync][NS::DataBuffer] Normalized Vector2") {
	NS::DataBuffer::CompressionLevel compression_level = NS::DataBuffer::COMPRESSION_LEVEL_3;
	double epsilon = 0.0;

	SUBCASE("[NetSync][NS::DataBuffer] Compression level 3") {
		compression_level = NS::DataBuffer::COMPRESSION_LEVEL_3;
		epsilon = 0.033335;
	}

	SUBCASE("[NetSync][NS::DataBuffer] Compression level 2") {
		compression_level = NS::DataBuffer::COMPRESSION_LEVEL_2;
		epsilon = 0.007935;
	}

	SUBCASE("[NetSync][NS::DataBuffer] Compression level 1") {
		compression_level = NS::DataBuffer::COMPRESSION_LEVEL_1;
		epsilon = 0.00196;
	}

	SUBCASE("[NetSync][NS::DataBuffer] Compression level 0") {
		compression_level = NS::DataBuffer::COMPRESSION_LEVEL_0;
		epsilon = 0.00049;
	}

	const Vector<double> values = real_values(compression_level);
	for (int i = 0; i < values.size(); ++i) {
		NS::DataBuffer buffer;

		Vector2 value = Vector2(values[i], values[i]);
		if (value.is_equal_approx(Vector2(0, 0)) == false) {
			value = value.normalized();
			if (Math::is_nan(value.x) || Math::is_nan(value.y)) {
				// This can happen in case of `FLT_MAX` is used.
				continue;
			}
		}

		buffer.begin_write(0);
		const Vector2 added_value = buffer.add_normalized_vector2(value, compression_level);
		CHECK_MESSAGE(added_value.x == doctest::Approx(value.x).epsilon(epsilon), "Added Vector2 should have the same x axis");
		CHECK_MESSAGE(added_value.y == doctest::Approx(value.y).epsilon(epsilon), "Added Vector2 should have the same y axis");

		CHECK(buffer.get_bit_offset() == buffer.get_bit_taken(NS::DataBuffer::DATA_TYPE_NORMALIZED_VECTOR2, compression_level));

		buffer.begin_read();
		const Vector2 read_value = buffer.read_normalized_vector2(compression_level);
		CHECK_MESSAGE(read_value.x == doctest::Approx(value.x).epsilon(epsilon), "Read Vector2 should have the same x axis");
		CHECK_MESSAGE(read_value.y == doctest::Approx(value.y).epsilon(epsilon), "Read Vector2 should have the same y axis");
	}
}

TEST_CASE("[NetSync][NS::DataBuffer] Vector3") {
	NS::DataBuffer::CompressionLevel compression_level = {};

	SUBCASE("[NetSync][NS::DataBuffer] Compression level 3") {
		compression_level = NS::DataBuffer::COMPRESSION_LEVEL_3;
	}

	SUBCASE("[NetSync][NS::DataBuffer] Compression level 2") {
		compression_level = NS::DataBuffer::COMPRESSION_LEVEL_2;
	}

	SUBCASE("[NetSync][NS::DataBuffer] Compression level 1") {
		compression_level = NS::DataBuffer::COMPRESSION_LEVEL_1;
	}

	SUBCASE("[NetSync][NS::DataBuffer] Compression level 0") {
		compression_level = NS::DataBuffer::COMPRESSION_LEVEL_0;
	}

	const Vector<double> values = real_values(compression_level);
	const double epsilon = Math::pow(2.0, NS::DataBuffer::get_mantissa_bits(compression_level) - 1);
	for (int i = 0; i < values.size(); ++i) {
		NS::DataBuffer buffer;

#ifdef REAL_T_IS_DOUBLE
		const Vector3 value = Vector3(values[i], values[i], values[i]);
#else
		const real_t clamped_value = CLAMP(values[i], -FLT_MIN, FLT_MAX);
		const Vector3 value = Vector3(clamped_value, clamped_value, clamped_value);
#endif

		buffer.begin_write(0);
		const Vector3 added_value = buffer.add_vector3(value, compression_level);
		CHECK_MESSAGE(added_value.x == doctest::Approx(value.x).epsilon(epsilon), "Added Vector3 should have the same x axis");
		CHECK_MESSAGE(added_value.y == doctest::Approx(value.y).epsilon(epsilon), "Added Vector3 should have the same y axis");
		CHECK_MESSAGE(added_value.z == doctest::Approx(value.z).epsilon(epsilon), "Added Vector3 should have the same z axis");

		CHECK(buffer.get_bit_offset() == buffer.get_bit_taken(NS::DataBuffer::DATA_TYPE_VECTOR3, compression_level));

		buffer.begin_read();
		const Vector3 read_value = buffer.read_vector3(compression_level);
		CHECK_MESSAGE(read_value.x == doctest::Approx(value.x).epsilon(epsilon), "Read Vector3 should have the same x axis");
		CHECK_MESSAGE(read_value.y == doctest::Approx(value.y).epsilon(epsilon), "Read Vector3 should have the same y axis");
		CHECK_MESSAGE(read_value.z == doctest::Approx(value.z).epsilon(epsilon), "Read Vector3 should have the same z axis");
	}
}

TEST_CASE("[NetSync][NS::DataBuffer] Normalized Vector3") {
	NS::DataBuffer::CompressionLevel compression_level = NS::DataBuffer::COMPRESSION_LEVEL_3;
	double epsilon = 0.0;

	SUBCASE("[NetSync][NS::DataBuffer] Compression level 3") {
		compression_level = NS::DataBuffer::COMPRESSION_LEVEL_3;
		epsilon = 0.033335;
	}

	SUBCASE("[NetSync][NS::DataBuffer] Compression level 2") {
		compression_level = NS::DataBuffer::COMPRESSION_LEVEL_2;
		epsilon = 0.007935;
	}

	SUBCASE("[NetSync][NS::DataBuffer] Compression level 1") {
		compression_level = NS::DataBuffer::COMPRESSION_LEVEL_1;
		epsilon = 0.00196;
	}

	SUBCASE("[NetSync][NS::DataBuffer] Compression level 0") {
		compression_level = NS::DataBuffer::COMPRESSION_LEVEL_0;
		epsilon = 0.00049;
	}

	const Vector<double> values = real_values(compression_level);
	for (int i = 0; i < values.size(); ++i) {
		NS::DataBuffer buffer;

		Vector3 value = Vector3(values[i], values[i], values[i]);
		if (value.is_equal_approx(Vector3(0, 0, 0)) == false) {
			value = value.normalized();
			if (Math::is_nan(value.x) || Math::is_nan(value.y) || Math::is_nan(value.z)) {
				// This can happen in case of `FLT_MAX` is used.
				continue;
			}
		}

		CHECK(buffer.get_bit_offset() == 0);
		buffer.begin_write(0);

		CHECK(buffer.get_bit_offset() == 0);

		const Vector3 added_value = buffer.add_normalized_vector3(value, compression_level);
		CHECK_MESSAGE(added_value.x == doctest::Approx(value.x).epsilon(epsilon), "Added Vector3 should have the same x axis");
		CHECK_MESSAGE(added_value.y == doctest::Approx(value.y).epsilon(epsilon), "Added Vector3 should have the same y axis");
		CHECK_MESSAGE(added_value.z == doctest::Approx(value.z).epsilon(epsilon), "Added Vector3 should have the same z axis");

		CHECK(buffer.get_bit_offset() == buffer.get_bit_taken(NS::DataBuffer::DATA_TYPE_NORMALIZED_VECTOR3, compression_level));

		buffer.begin_read();
		const Vector3 read_value = buffer.read_normalized_vector3(compression_level);
		CHECK_MESSAGE(read_value.x == doctest::Approx(value.x).epsilon(epsilon), "Read Vector3 should have the same x axis");
		CHECK_MESSAGE(read_value.y == doctest::Approx(value.y).epsilon(epsilon), "Read Vector3 should have the same y axis");
		CHECK_MESSAGE(read_value.z == doctest::Approx(value.z).epsilon(epsilon), "Read Vector3 should have the same z axis");
	}
}

TEST_CASE("[NetSync][NS::DataBuffer] Normalized Vector2(0, 0)") {
	NS::DataBuffer buffer;

	NS::DataBuffer::CompressionLevel compression_level = NS::DataBuffer::COMPRESSION_LEVEL_1;
	real_t epsilon = 0.033335;
	Vector2 value = Vector2(0, 0);

	buffer.begin_write(0);
	const Vector2 added_value = buffer.add_normalized_vector2(value, compression_level);
	CHECK_MESSAGE(added_value.x == doctest::Approx(value.x).epsilon(epsilon), "Added Vector2 should have the same x axis.");
	CHECK_MESSAGE(added_value.y == doctest::Approx(value.y).epsilon(epsilon), "Added Vector2 should have the same y axis.");

	CHECK(buffer.get_bit_offset() == buffer.get_bit_taken(NS::DataBuffer::DATA_TYPE_NORMALIZED_VECTOR2, compression_level));

	buffer.begin_read();
	const Vector2 read_value = buffer.read_normalized_vector2(compression_level);
	CHECK_MESSAGE(read_value.x == doctest::Approx(value.x).epsilon(epsilon), "Read Vector2 should have the same x axis");
	CHECK_MESSAGE(read_value.y == doctest::Approx(value.y).epsilon(epsilon), "Read Vector2 should have the same y axis");
}

TEST_CASE("[NetSync][NS::DataBuffer] Variant") {
	Variant value = {};

	SUBCASE("[NetSync][NS::DataBuffer] Invalid value") {
		value = {};
	}
	SUBCASE("[NetSync][NS::DataBuffer] String") {
		value = "VariantString";
	}
	SUBCASE("[NetSync][NS::DataBuffer] Vector") {
		value = sarray("VariantString1", "VariantString2", "VariantString3");
	}
	SUBCASE("[NetSync][NS::DataBuffer] Dictionary") {
		Dictionary dictionary;
		dictionary[1] = "Value";
		dictionary["Key"] = -1;
		value = dictionary;
	}
	SUBCASE("[NetSync][NS::DataBuffer] Array") {
		Array array;
		array.append("VariantString");
		array.append(0);
		array.append(-1.2);
		value = array;
	}

	NS::DataBuffer buffer;
	buffer.begin_write(0);
	CHECK_MESSAGE(buffer.add_variant(value) == value, "Should return the same value");

	const int size = buffer.get_bit_offset();
	buffer.begin_read();
	CHECK(size == buffer.read_variant_size());

	buffer.begin_read();
	CHECK_MESSAGE(buffer.read_variant() == value, "Should read the same value");
}

TEST_CASE("[NetSync][NS::DataBuffer] Seek") {
	NS::DataBuffer buffer;
	buffer.begin_write(0);
	buffer.add_bool(true);
	buffer.add_bool(false);
	buffer.begin_read();

	ERR_PRINT_OFF
	buffer.seek(-1);
	CHECK_MESSAGE(buffer.get_bit_offset() == 0, "Bit offset should fail for negative values");
	ERR_PRINT_ON

	buffer.seek(1);
	CHECK_MESSAGE(buffer.get_bit_offset() == 1, "Bit offset should be 1 after seek to 1");
	CHECK_MESSAGE(buffer.read_bool() == false, "Should read false at position 1");

	buffer.seek(0);
	CHECK_MESSAGE(buffer.get_bit_offset() == 0, "Bit offset should be 0 after seek to 0");
	CHECK_MESSAGE(buffer.read_bool() == true, "Should read true at position 0");
}

TEST_CASE("[NetSync][NS::DataBuffer] Metadata") {
	bool value = {};
	bool metadata = {};

	SUBCASE("[NetSync][NS::DataBuffer] True") {
		metadata = true;
		value = false;
	}

	SUBCASE("[NetSync][NS::DataBuffer] False") {
		metadata = false;
		value = true;
	}

	const int metadata_size = NS::DataBuffer::get_bit_taken(NS::DataBuffer::DATA_TYPE_BOOL, NS::DataBuffer::COMPRESSION_LEVEL_0);
	NS::DataBuffer buffer;
	buffer.begin_write(metadata_size);
	buffer.add_bool(metadata);
	buffer.add_bool(value);
	buffer.begin_read();
	CHECK_MESSAGE(buffer.read_bool() == metadata, "Should return correct metadata");
	CHECK_MESSAGE(buffer.read_bool() == value, "Should return correct value after metadata");
	CHECK_MESSAGE(buffer.get_metadata_size() == metadata_size, "Metadata size should be equal to expected");
	CHECK_MESSAGE(buffer.size() == NS::DataBuffer::get_bit_taken(NS::DataBuffer::DATA_TYPE_BOOL, NS::DataBuffer::COMPRESSION_LEVEL_0), "Size should be equal to expected");
	CHECK_MESSAGE(buffer.total_size() == NS::DataBuffer::get_bit_taken(NS::DataBuffer::DATA_TYPE_BOOL, NS::DataBuffer::COMPRESSION_LEVEL_0) + metadata_size, "Total size should be equal to expected");
}

TEST_CASE("[NetSync][NS::DataBuffer] Zero") {
	constexpr NS::DataBuffer::CompressionLevel compression = NS::DataBuffer::COMPRESSION_LEVEL_0;
	NS::DataBuffer buffer;
	buffer.begin_write(0);
	buffer.add_int(-1, compression);
	buffer.zero();
	buffer.begin_read();
	CHECK_MESSAGE(buffer.read_int(compression) == 0, "Should return 0");
}

TEST_CASE("[NetSync][NS::DataBuffer] Shrinking") {
	NS::DataBuffer buffer;
	buffer.begin_write(0);
	for (int i = 0; i < 2; ++i) {
		buffer.add_real(3.14, NS::DataBuffer::COMPRESSION_LEVEL_0);
	}
	const int original_size = buffer.total_size();

	ERR_PRINT_OFF;
	buffer.shrink_to(0, original_size + 1);
	ERR_PRINT_ON;
	CHECK_MESSAGE(buffer.total_size() == original_size, "Shrinking to a larger size should fail.");

	ERR_PRINT_OFF;
	buffer.shrink_to(0, -1);
	ERR_PRINT_ON;
	CHECK_MESSAGE(buffer.total_size() == original_size, "Shrinking with a negative bits size should fail.");

	buffer.shrink_to(0, original_size - 8);
	CHECK_MESSAGE(buffer.total_size() == original_size - 8, "Shrinking by 1 byte should succeed.");
	CHECK_MESSAGE(buffer.get_buffer().size_in_bits() == original_size, "Buffer size after shrinking by 1 byte should be the same.");

	buffer.dry();
	CHECK_MESSAGE(buffer.get_buffer().size_in_bits() == original_size - 8, "Buffer size after dry should changed to the smallest posiible.");
}

TEST_CASE("[NetSync][NS::DataBuffer] Skip") {
	const bool value = true;

	NS::DataBuffer buffer;
	buffer.add_bool(!value);
	buffer.add_bool(value);

	buffer.begin_read();
	buffer.seek(NS::DataBuffer::get_bit_taken(NS::DataBuffer::DATA_TYPE_BOOL, NS::DataBuffer::COMPRESSION_LEVEL_0));
	CHECK_MESSAGE(buffer.read_bool() == value, "Should read the same value");
}
} //namespace test_netsync_NS::DataBuffer

#endif
