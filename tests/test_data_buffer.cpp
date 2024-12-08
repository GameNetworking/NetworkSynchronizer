#include "test_data_buffer.h"

#include "../core/data_buffer.h"
#include "../core/ensure.h"
#include "../core/net_math.h"
#include "../core/scene_synchronizer_debugger.h"

#include <tuple>

NS::SceneSynchronizerDebugger debugger;

inline std::vector<std::int64_t> int_values(NS::DataBuffer::CompressionLevel p_compression_level) {
	std::vector<int64_t> values;
	values.push_back(0);
	values.push_back(1);
	values.push_back(-4);
	values.push_back(6);
	values.push_back(-15);
	values.push_back(-100);
	values.push_back(100);

	switch (p_compression_level) {
		case NS::DataBuffer::COMPRESSION_LEVEL_3: {
			values.push_back(127);
			values.push_back(-128);
		}
		break;
		case NS::DataBuffer::COMPRESSION_LEVEL_2: {
			values.push_back(32767);
			values.push_back(-32768);
		}
		break;
		case NS::DataBuffer::COMPRESSION_LEVEL_1: {
			values.push_back(2147483647);
			values.push_back(-2147483648LL);
		}
		break;
		case NS::DataBuffer::COMPRESSION_LEVEL_0: {
			values.push_back(2147483647);
			values.push_back(-9223372036854775807LL);
		}
		break;
	}

	return values;
}

inline std::vector<std::uint8_t> byte_values() {
	std::vector<uint8_t> values;
	values.push_back(0);
	values.push_back(44);
	values.push_back(10);
	values.push_back(100);
	values.push_back(std::numeric_limits<uint8_t>::max());
	values.push_back(std::numeric_limits<uint8_t>::max() / 2);
	values.push_back(std::numeric_limits<uint8_t>::max() / 3);
	values.push_back(std::numeric_limits<uint8_t>::max() / 4);
	return values;
}

inline std::vector<std::uint64_t> uint_values(NS::DataBuffer::CompressionLevel p_compression_level) {
	std::vector<uint64_t> values;
	values.push_back(0);
	values.push_back(44);
	values.push_back(10);
	values.push_back(100);
	values.push_back(std::numeric_limits<uint8_t>::max());

	switch (p_compression_level) {
		case NS::DataBuffer::COMPRESSION_LEVEL_3: {
		}
		break;
		case NS::DataBuffer::COMPRESSION_LEVEL_2: {
			values.push_back(32767);
		}
		break;
		case NS::DataBuffer::COMPRESSION_LEVEL_1: {
			values.push_back(32767);
			values.push_back(std::numeric_limits<uint32_t>::max());
		}
		break;
		case NS::DataBuffer::COMPRESSION_LEVEL_0: {
			values.push_back(32767);
			values.push_back(std::numeric_limits<uint32_t>::max());
			values.push_back(std::numeric_limits<uint64_t>::max());
		}
		break;
	}

	return values;
}

template <typename T>
inline std::vector<T> real_values(NS::DataBuffer::CompressionLevel p_compression_level) {
	std::vector<T> values;
	values.push_back(T(M_PI));
	values.push_back(T(0.0));
	values.push_back(T(-3.04));
	values.push_back(T(3.04));
	values.push_back(T(0.5));
	values.push_back(T(-0.5));
	values.push_back(T(1));
	values.push_back(T(-1));
	values.push_back(T(0.9));
	values.push_back(T(-0.9));
	values.push_back(T(3.9));
	values.push_back(T(-3.9));
	values.push_back(T(8));
	values.push_back(T(0.00001));
	values.push_back(T(-0.00001));
	values.push_back(T(0.0001));
	values.push_back(T(-0.0001));
	values.push_back(T(0.001));
	values.push_back(T(-0.001));
	values.push_back(T(0.01));
	values.push_back(T(-0.01));
	values.push_back(T(0.1));
	values.push_back(T(-0.1));

	switch (p_compression_level) {
		case NS::DataBuffer::COMPRESSION_LEVEL_3: {
			values.push_back(T(-15'360 / 2.));
			values.push_back(T(15'360 / 2.));
			values.push_back(T(-15'360));
			values.push_back(T(15'360));
		}
		break;
		case NS::DataBuffer::COMPRESSION_LEVEL_2: {
			// https://en.wikipedia.org/wiki/Half-precision_floating-point_format#Half_precision_examples
			values.push_back(T(-65'504));
			values.push_back(T(65'504));
			values.push_back(T(std::pow(2.0, -14) / 1024));
			values.push_back(T(std::pow(2.0, -14) * 1023 / 1024));
			values.push_back(T(std::pow(2.0, -1) * (1 + 1023.0 / 1024)));
			values.push_back(T((1 + 1.0 / 1024)));
		}
		break;
		case NS::DataBuffer::COMPRESSION_LEVEL_1: {
			// https://en.wikipedia.org/wiki/Single-precision_floating-point_format#Single-precision_examples
			values.push_back(T(std::numeric_limits<float>::min()));
			values.push_back(T(std::numeric_limits<float>::max()));
			values.push_back(T(-std::numeric_limits<float>::max()));
			values.push_back(T(std::pow(2.0, -149)));
			values.push_back(T(std::pow(2.0, -126) * (1.0 - std::pow(2.0, -23))));
			values.push_back(T(1.0 - std::pow(2.0, -24)));
			values.push_back(T(1.0 + std::pow(2.0, -23)));
		}
		break;
		case NS::DataBuffer::COMPRESSION_LEVEL_0: {
			// https://en.wikipedia.org/wiki/Double-precision_floating-point_format#Double-precision_examples
			if constexpr (std::is_same<T, double>::value) {
				values.push_back(T(std::numeric_limits<double>::min()));
				values.push_back(T(std::numeric_limits<double>::max()));
				values.push_back(T(-std::numeric_limits<double>::max()));
			}
			values.push_back(T(1.0000000000000002));
			values.push_back(T(4.9406564584124654 * std::pow(10.0, -324.0)));
			values.push_back(T(2.2250738585072009 * std::pow(10.0, -308.0)));
		}
		break;
	}

	return values;
}

template <typename T>
inline std::vector<T> unit_real_values(NS::DataBuffer::CompressionLevel p_compression_level) {
	std::vector<T> values;
	values.push_back(T(0.0));
	values.push_back(T(0.1));
	values.push_back(T(0.2));
	values.push_back(T(0.3));
	values.push_back(T(0.4));
	values.push_back(T(0.5));
	values.push_back(T(0.6));
	values.push_back(T(0.7));
	values.push_back(T(0.7));
	values.push_back(T(0.8));
	values.push_back(T(0.9));
	values.push_back(T(0.05));
	values.push_back(T(0.15));
	values.push_back(T(0.25));
	values.push_back(T(0.35));
	values.push_back(T(0.45));
	values.push_back(T(0.55));
	values.push_back(T(0.65));
	values.push_back(T(0.75));
	values.push_back(T(0.85));
	values.push_back(T(0.95));
	values.push_back(T(1.0));
	return values;
}

template <typename T>
inline std::vector<std::pair<T, T>> vector_2_values(NS::DataBuffer::CompressionLevel p_compression_level) {
	std::vector<std::pair<T, T>> values;
	values.push_back(std::make_pair(T(0.0), T(0.0)));
	values.push_back(std::make_pair(T(1.0), T(1.0)));
	values.push_back(std::make_pair(T(-1.0), T(-1.0)));
	values.push_back(std::make_pair(T(-1.0), T(1.0)));
	values.push_back(std::make_pair(T(1.0), T(-1.0)));

	values.push_back(std::make_pair(T(100.0), T(-1.0)));
	values.push_back(std::make_pair(T(-1.0), T(100.0)));
	values.push_back(std::make_pair(T(-100.0), T(1.0)));
	values.push_back(std::make_pair(T(-1802.0), T(-100.0)));
	values.push_back(std::make_pair(T(-1102.0), T(1290.0)));

	const T epsilon = NS::DataBuffer::get_real_epsilon<T>(NS::DataBuffer::DATA_TYPE_REAL, p_compression_level);
	values.push_back(std::make_pair(epsilon, epsilon));
	values.push_back(std::make_pair(T(0.0), epsilon));
	values.push_back(std::make_pair(epsilon, T(0.0)));
	values.push_back(std::make_pair(-epsilon, -epsilon));
	values.push_back(std::make_pair(T(0.0), -epsilon));
	values.push_back(std::make_pair(-epsilon, T(0.0)));
	values.push_back(std::make_pair(epsilon, -epsilon));
	values.push_back(std::make_pair(-epsilon, epsilon));

	return values;
}

template <typename T>
inline std::vector<std::pair<T, T>> normalized_vector_2_values(NS::DataBuffer::CompressionLevel p_compression_level) {
	std::vector<std::pair<T, T>> values;
	values.push_back(std::make_pair(T(0.0), T(0.0)));
	values.push_back(std::make_pair(T(1.0), T(0.0)));
	values.push_back(std::make_pair(T(-1.0), T(0.0)));
	values.push_back(std::make_pair(T(0.0), T(1.0)));
	values.push_back(std::make_pair(T(0.0), T(-1.0)));
	values.push_back(std::make_pair(T(0.5), T(0.5)));
	values.push_back(std::make_pair(T(-0.5), T(-0.5)));
	values.push_back(std::make_pair(T(0.5), T(-0.5)));
	values.push_back(std::make_pair(T(-0.5), T(0.5)));
	values.push_back(std::make_pair(T(-0.7), T(0.5)));
	values.push_back(std::make_pair(T(0.7), T(0.2)));
	values.push_back(std::make_pair(T(0.7), T(-0.2)));
	values.push_back(std::make_pair(T(0.99), T(-0.2)));
	values.push_back(std::make_pair(T(-0.99), T(-0.99)));
	values.push_back(std::make_pair(T(0.22), T(-0.33)));

	for (auto &value : values) {
		NS::MathFunc::vec2_normalize<T>(value.first, value.second);
	}

	return values;
}

template <typename T>
inline std::vector<std::tuple<T, T, T>> normalized_vector_3_values(NS::DataBuffer::CompressionLevel p_compression_level) {
	std::vector<std::tuple<T, T, T>> values;
	values.push_back(std::make_tuple(T(0.0), T(0.0), T(0.0)));
	values.push_back(std::make_tuple(T(1.0), T(0.0), T(0.0)));
	values.push_back(std::make_tuple(T(-1.0), T(0.0), T(0.0)));
	values.push_back(std::make_tuple(T(0.0), T(1.0), T(0.0)));
	values.push_back(std::make_tuple(T(0.0), T(-1.0), T(0.0)));
	values.push_back(std::make_tuple(T(0.5), T(0.5), T(0.0)));
	values.push_back(std::make_tuple(T(-0.5), T(-0.5), T(0.0)));
	values.push_back(std::make_tuple(T(0.5), T(-0.5), T(0.0)));
	values.push_back(std::make_tuple(T(-0.5), T(0.5), T(0.0)));
	values.push_back(std::make_tuple(T(-0.7), T(0.5), T(0.0)));
	values.push_back(std::make_tuple(T(0.7), T(0.2), T(0.0)));
	values.push_back(std::make_tuple(T(0.7), T(-0.2), T(0.0)));
	values.push_back(std::make_tuple(T(0.99), T(-0.2), T(0.0)));
	values.push_back(std::make_tuple(T(-0.99), T(-0.99), T(0.0)));
	values.push_back(std::make_tuple(T(0.22), T(-0.33), T(0.0)));
	values.push_back(std::make_tuple(T(-0.5), T(-0.5), T(1.0)));
	values.push_back(std::make_tuple(T(0.5), T(-0.5), T(1.0)));
	values.push_back(std::make_tuple(T(-0.5), T(0.5), T(-1.0)));
	values.push_back(std::make_tuple(T(-0.7), T(0.5), T(-1.0)));
	values.push_back(std::make_tuple(T(0.7), T(0.2), T(-1.0)));
	values.push_back(std::make_tuple(T(0.7), T(-0.2), T(-0.2)));
	values.push_back(std::make_tuple(T(0.99), T(-0.2), T(0.3)));
	values.push_back(std::make_tuple(T(-0.99), T(-0.99), T(0.8)));
	values.push_back(std::make_tuple(T(-0.5), T(-0.5), T(-0.3)));
	values.push_back(std::make_tuple(T(0.5), T(-0.5), T(-0.9)));
	values.push_back(std::make_tuple(T(-0.5), T(0.5), T(-0.2)));
	values.push_back(std::make_tuple(T(-0.7), T(0.5), T(-0.4)));

	for (auto &value : values) {
		NS::MathFunc::vec3_normalize<T>(std::get<0>(value), std::get<1>(value), std::get<2>(value));
	}

	return values;
}

template <typename T>
inline std::vector<std::tuple<T, T, T>> vector_3_values(NS::DataBuffer::CompressionLevel p_compression_level) {
	std::vector<std::tuple<T, T, T>> values;
	values.push_back(std::make_tuple(T(0.0), T(0.0), T(0.0)));
	values.push_back(std::make_tuple(T(1.0), T(1.0), T(1.0)));
	values.push_back(std::make_tuple(T(-1.0), T(-1.0), T(-1.0)));
	values.push_back(std::make_tuple(T(-1.0), T(1.0), T(0.0)));
	values.push_back(std::make_tuple(T(1.0), T(-1.0), T(1.0)));

	values.push_back(std::make_tuple(T(100.0), T(-1.0), T(200.0)));
	values.push_back(std::make_tuple(T(-1.0), T(100.0), T(300.0)));
	values.push_back(std::make_tuple(T(-100.0), T(1.0), T(211.0)));
	values.push_back(std::make_tuple(T(-1802.0), T(-100.0), T(811.0)));
	values.push_back(std::make_tuple(T(-1102.0), T(1290.0), T(-1000.0)));

	const T epsilon = NS::DataBuffer::get_real_epsilon<T>(NS::DataBuffer::DATA_TYPE_REAL, p_compression_level);
	values.push_back(std::make_tuple(epsilon, epsilon, epsilon));
	values.push_back(std::make_tuple(T(0.0), epsilon, T(0.0)));
	values.push_back(std::make_tuple(epsilon, T(0.0), epsilon));
	values.push_back(std::make_tuple(-epsilon, -epsilon, -epsilon));
	values.push_back(std::make_tuple(T(0.0), -epsilon, epsilon));
	values.push_back(std::make_tuple(-epsilon, T(0.0), epsilon));
	values.push_back(std::make_tuple(epsilon, -epsilon, -epsilon));
	values.push_back(std::make_tuple(-epsilon, epsilon, -epsilon));

	return values;
}

void test_data_buffer_unaligned_write_read() {
	NS::DataBuffer db;
	db.begin_write(debugger, 0);

	NS_ASSERT_COND(!db.is_buffer_failed());

	const bool v1 = false;
	const bool v2 = true;
	const bool v3 = false;
	const bool v4 = true;
	const std::uint16_t v5 = 2;
	const bool v6 = true;
	const int v7 = 2;
	const bool v8 = false;
	const bool v9 = true;
	const int v10 = 2;
	const std::uint8_t v11 = 0;

	db.add(v1);
	NS_ASSERT_COND(!db.is_buffer_failed());

	db.add(v2);
	NS_ASSERT_COND(!db.is_buffer_failed());

	db.add(v3);
	NS_ASSERT_COND(!db.is_buffer_failed());

	db.add(v4);
	NS_ASSERT_COND(!db.is_buffer_failed());

	db.add(v5);
	NS_ASSERT_COND(!db.is_buffer_failed());

	db.add(v6);
	NS_ASSERT_COND(!db.is_buffer_failed());

	db.add(v7);
	NS_ASSERT_COND(!db.is_buffer_failed());

	db.add(v8);
	NS_ASSERT_COND(!db.is_buffer_failed());

	db.add(v9);
	NS_ASSERT_COND(!db.is_buffer_failed());

	db.add(v10);
	NS_ASSERT_COND(!db.is_buffer_failed());

	db.add(v11);
	NS_ASSERT_COND(!db.is_buffer_failed());

	db.begin_read(debugger);

	bool o1;
	bool o2;
	bool o3;
	bool o4;
	std::uint16_t o5 = std::numeric_limits<std::uint16_t>::max();
	bool o6;
	int o7;
	bool o8;
	bool o9;
	int o10;
	std::uint8_t o11;

	db.read(o1);
	NS_ASSERT_COND(!db.is_buffer_failed());
	NS_ASSERT_COND(v1 == o1);

	db.read(o2);
	NS_ASSERT_COND(!db.is_buffer_failed());
	NS_ASSERT_COND(v2 == o2);

	db.read(o3);
	NS_ASSERT_COND(!db.is_buffer_failed());
	NS_ASSERT_COND(v3 == o3);

	db.read(o4);
	NS_ASSERT_COND(!db.is_buffer_failed());
	NS_ASSERT_COND(v4 == o4);

	db.read(o5);
	NS_ASSERT_COND(!db.is_buffer_failed());
	NS_ASSERT_COND(v5 == o5);

	db.read(o6);
	NS_ASSERT_COND(!db.is_buffer_failed());
	NS_ASSERT_COND(v6 == o6);

	db.read(o7);
	NS_ASSERT_COND(!db.is_buffer_failed());
	NS_ASSERT_COND(v7 == o7);

	db.read(o8);
	NS_ASSERT_COND(!db.is_buffer_failed());
	NS_ASSERT_COND(v8 == o8);

	db.read(o9);
	NS_ASSERT_COND(!db.is_buffer_failed());
	NS_ASSERT_COND(v9 == o9);

	db.read(o10);
	NS_ASSERT_COND(!db.is_buffer_failed());
	NS_ASSERT_COND(v10 == o10);

	db.read(o11);
	NS_ASSERT_COND(!db.is_buffer_failed());
	NS_ASSERT_COND(v11 == o11);
}

void test_data_buffer_string() {
	NS::DataBuffer db;
	db.begin_write(debugger, 0);

	std::string abc_1("abc_1");
	db.add(abc_1);

	db.begin_read(debugger);
	std::string abc_1_r;
	db.read(abc_1_r);

	NS_ASSERT_COND(abc_1 == abc_1_r);
}

void test_data_buffer_u16string() {
	{
		NS::DataBuffer db;
		db.begin_write(debugger, 0);

		std::u16string abc_1(u"abc_1");
		db.add(abc_1);

		std::string abc_2("abc_2");
		db.add(abc_2);

		std::u16string abc_3(u"abc_3");
		db.add(abc_3);

		db.begin_read(debugger);
		std::u16string abc_1_r;
		db.read(abc_1_r);

		std::string abc_2_r;
		db.read(abc_2_r);

		std::u16string abc_3_r;
		db.read(abc_3_r);

		NS_ASSERT_COND(abc_1 == abc_1_r);
		NS_ASSERT_COND(abc_2 == abc_2_r);
		NS_ASSERT_COND(abc_3 == abc_3_r);
	}
}

void test_data_buffer_bool() {
	{
		NS::DataBuffer buffer;

		buffer.begin_write(debugger, 0);
		buffer.add_bool(true);

		NS_ASSERT_COND(buffer.get_bit_offset() == buffer.get_bit_taken(NS::DataBuffer::DATA_TYPE_BOOL, NS::DataBuffer::COMPRESSION_LEVEL_0));

		buffer.begin_read(debugger);
		NS_ASSERT_COND_MSG(buffer.read_bool() == true, "Should read the same value");
	}
	{
		NS::DataBuffer buffer;

		buffer.begin_write(debugger, 0);
		buffer.add_bool(false);

		NS_ASSERT_COND(buffer.get_bit_offset() == buffer.get_bit_taken(NS::DataBuffer::DATA_TYPE_BOOL, NS::DataBuffer::COMPRESSION_LEVEL_0));

		buffer.begin_read(debugger);
		NS_ASSERT_COND_MSG(buffer.read_bool() == false, "Should read the same value");
	}
}

void test_data_buffer_int() {
	for (int cl = (int)NS::DataBuffer::COMPRESSION_LEVEL_0; cl <= (int)NS::DataBuffer::COMPRESSION_LEVEL_3; cl++) {
		const NS::DataBuffer::CompressionLevel compression_level = (NS::DataBuffer::CompressionLevel)cl;

		const std::vector<std::int64_t> values = int_values(compression_level);

		for (int i = 0; i < values.size(); ++i) {
			NS::DataBuffer buffer;
			buffer.begin_write(debugger, 0);

			const std::int64_t value = values[i];

			buffer.add_int(value, compression_level);
			NS_ASSERT_COND(buffer.get_bit_offset() == buffer.get_bit_taken(NS::DataBuffer::DATA_TYPE_INT, compression_level));
			NS_ASSERT_COND(!buffer.is_buffer_failed());

			buffer.begin_read(debugger);
			const std::int64_t read_value = buffer.read_int(compression_level);
			const bool is_equal = read_value == value;
			NS_ASSERT_COND(!buffer.is_buffer_failed());
			NS_ASSERT_COND_MSG(is_equal, "Should read the same value. Written(" + std::to_string(value) + ") Read(" + std::to_string(read_value) + ")");
		}
	}
}

void test_data_buffer_uint() {
	for (int cl = (int)NS::DataBuffer::COMPRESSION_LEVEL_0; cl <= (int)NS::DataBuffer::COMPRESSION_LEVEL_3; cl++) {
		const NS::DataBuffer::CompressionLevel compression_level = (NS::DataBuffer::CompressionLevel)cl;

		const std::vector<std::uint64_t> values = uint_values(compression_level);

		for (int i = 0; i < values.size(); ++i) {
			NS::DataBuffer buffer;
			buffer.begin_write(debugger, 0);

			const std::uint64_t value = values[i];

			buffer.add_uint(value, compression_level);
			NS_ASSERT_COND(buffer.get_bit_offset() == buffer.get_bit_taken(NS::DataBuffer::DATA_TYPE_UINT, compression_level));
			NS_ASSERT_COND(!buffer.is_buffer_failed());

			buffer.begin_read(debugger);
			const std::uint64_t read_value = buffer.read_uint(compression_level);
			const bool is_equal = read_value == value;
			NS_ASSERT_COND(!buffer.is_buffer_failed());
			NS_ASSERT_COND_MSG(is_equal, "Should read the same value. Written(" + std::to_string(value) + ") Read(" + std::to_string(read_value) + ")");
		}
	}
}

template <typename T>
void test_data_buffer_real() {
	for (int cl = (int)NS::DataBuffer::COMPRESSION_LEVEL_0; cl <= (int)NS::DataBuffer::COMPRESSION_LEVEL_3; cl++) {
		const NS::DataBuffer::CompressionLevel compression_level = (NS::DataBuffer::CompressionLevel)cl;
		const T epsilon = NS::DataBuffer::get_real_epsilon<T>(NS::DataBuffer::DATA_TYPE_REAL, compression_level);

		const std::vector<T> values = real_values<T>(compression_level);

		for (int i = 0; i < values.size(); ++i) {
			NS::DataBuffer buffer;
			buffer.begin_write(debugger, 0);

			const T value = values[i];

			buffer.add_real(value, compression_level);
			if (std::is_same<T, float>::value && compression_level == NS::DataBuffer::COMPRESSION_LEVEL_0) {
				// Fallback to compression level 1
				NS_ASSERT_COND(buffer.get_bit_offset() == buffer.get_bit_taken(NS::DataBuffer::DATA_TYPE_REAL, NS::DataBuffer::COMPRESSION_LEVEL_1));
			} else {
				NS_ASSERT_COND(buffer.get_bit_offset() == buffer.get_bit_taken(NS::DataBuffer::DATA_TYPE_REAL, compression_level));
			}
			NS_ASSERT_COND(!buffer.is_buffer_failed());

			buffer.begin_read(debugger);
			T read_value;
			buffer.read_real(read_value, compression_level);
			const bool is_equal = NS::MathFunc::is_equal_approx<T>(read_value, value, epsilon);
			NS_ASSERT_COND(!buffer.is_buffer_failed());
			NS_ASSERT_COND_MSG(is_equal, "Should read the same value. Written(" + std::to_string(value) + ") Read(" + std::to_string(read_value) + ")");
		}
	}
}

template <typename T>
void test_data_buffer_positive_unit_real() {
	for (int cl = (int)NS::DataBuffer::COMPRESSION_LEVEL_0; cl <= (int)NS::DataBuffer::COMPRESSION_LEVEL_3; cl++) {
		const NS::DataBuffer::CompressionLevel compression_level = (NS::DataBuffer::CompressionLevel)cl;
		const T epsilon = NS::DataBuffer::get_real_epsilon<T>(NS::DataBuffer::DATA_TYPE_POSITIVE_UNIT_REAL, compression_level);

		const std::vector<T> values = unit_real_values<T>(compression_level);

		for (int i = 0; i < values.size(); ++i) {
			NS::DataBuffer buffer;
			buffer.begin_write(debugger, 0);

			const T value = values[i];

			buffer.add_positive_unit_real(value, compression_level);
			NS_ASSERT_COND(buffer.get_bit_offset() == buffer.get_bit_taken(NS::DataBuffer::DATA_TYPE_POSITIVE_UNIT_REAL, compression_level));
			NS_ASSERT_COND(!buffer.is_buffer_failed());

			buffer.begin_read(debugger);
			const T read_value = buffer.read_positive_unit_real(compression_level);
			const bool is_equal = NS::MathFunc::is_equal_approx<T>(read_value, value, epsilon);
			NS_ASSERT_COND(!buffer.is_buffer_failed());
			NS_ASSERT_COND_MSG(is_equal, "Should read the same value. Written(" + std::to_string(value) + ") Read(" + std::to_string(read_value) + ")");
		}
	}
}

template <typename T>
void test_data_buffer_unit_real() {
	for (int cl = (int)NS::DataBuffer::COMPRESSION_LEVEL_0; cl <= (int)NS::DataBuffer::COMPRESSION_LEVEL_3; cl++) {
		const NS::DataBuffer::CompressionLevel compression_level = (NS::DataBuffer::CompressionLevel)cl;
		const T epsilon = NS::DataBuffer::get_real_epsilon<T>(NS::DataBuffer::DATA_TYPE_UNIT_REAL, compression_level);

		const std::vector<T> values = unit_real_values<T>(compression_level);

		const float factors[] = { 1.0, -1.0 };
		for (float factor : factors) {
			for (int i = 0; i < values.size(); ++i) {
				NS::DataBuffer buffer;
				buffer.begin_write(debugger, 0);

				const T value = values[i] * factor;

				buffer.add_unit_real(value, compression_level);
				NS_ASSERT_COND(buffer.get_bit_offset() == buffer.get_bit_taken(NS::DataBuffer::DATA_TYPE_UNIT_REAL, compression_level));
				NS_ASSERT_COND(!buffer.is_buffer_failed());

				buffer.begin_read(debugger);
				const T read_value = buffer.read_unit_real(compression_level);
				const bool is_equal = NS::MathFunc::is_equal_approx<T>(read_value, value, epsilon);
				NS_ASSERT_COND(!buffer.is_buffer_failed());
				NS_ASSERT_COND_MSG(is_equal, "Should read the same value. Written(" + std::to_string(value) + ") Read(" + std::to_string(read_value) + ")");
			}
		}
	}
}

template <typename T>
void test_data_buffer_vector_2() {
	for (int cl = (int)NS::DataBuffer::COMPRESSION_LEVEL_0; cl <= (int)NS::DataBuffer::COMPRESSION_LEVEL_3; cl++) {
		const NS::DataBuffer::CompressionLevel compression_level = (NS::DataBuffer::CompressionLevel)cl;
		const T epsilon = NS::DataBuffer::get_real_epsilon<T>(NS::DataBuffer::DATA_TYPE_VECTOR2, compression_level);

		const std::vector<std::pair<T, T>> values = vector_2_values<T>(compression_level);

		for (int i = 0; i < values.size(); ++i) {
			NS::DataBuffer buffer;
			buffer.begin_write(debugger, 0);

			const std::pair<T, T> value = values[i];

			buffer.add_vector2(value.first, value.second, compression_level);
			if (std::is_same<T, float>::value && compression_level == NS::DataBuffer::COMPRESSION_LEVEL_0) {
				// Fallback to compression level 1
				NS_ASSERT_COND(buffer.get_bit_offset() == buffer.get_bit_taken(NS::DataBuffer::DATA_TYPE_VECTOR2, NS::DataBuffer::COMPRESSION_LEVEL_1));
			} else {
				NS_ASSERT_COND(buffer.get_bit_offset() == buffer.get_bit_taken(NS::DataBuffer::DATA_TYPE_VECTOR2, compression_level));
			}
			NS_ASSERT_COND(!buffer.is_buffer_failed());

			buffer.begin_read(debugger);
			T read_x;
			T read_y;
			buffer.read_vector2(read_x, read_y, compression_level);
			const bool is_equal = NS::MathFunc::is_equal_approx<T>(read_x, value.first, epsilon) && NS::MathFunc::is_equal_approx(read_y, value.second, epsilon);
			NS_ASSERT_COND(!buffer.is_buffer_failed());
			NS_ASSERT_COND_MSG(is_equal, "Should read the same value.");
		}
	}
}

template <typename T>
void test_data_buffer_normalized_vector_2() {
	for (int cl = (int)NS::DataBuffer::COMPRESSION_LEVEL_0; cl <= (int)NS::DataBuffer::COMPRESSION_LEVEL_3; cl++) {
		const NS::DataBuffer::CompressionLevel compression_level = (NS::DataBuffer::CompressionLevel)cl;
		const T epsilon = NS::DataBuffer::get_real_epsilon<T>(NS::DataBuffer::DATA_TYPE_NORMALIZED_VECTOR2, compression_level);

		const std::vector<std::pair<T, T>> values = normalized_vector_2_values<T>(compression_level);

		for (int i = 0; i < values.size(); ++i) {
			NS::DataBuffer buffer;
			buffer.begin_write(debugger, 0);

			const std::pair<T, T> value = values[i];

			buffer.add_normalized_vector2(value.first, value.second, compression_level);
			NS_ASSERT_COND(!buffer.is_buffer_failed());
			NS_ASSERT_COND(buffer.get_bit_offset() == buffer.get_bit_taken(NS::DataBuffer::DATA_TYPE_NORMALIZED_VECTOR2, compression_level));

			buffer.begin_read(debugger);
			T read_x;
			T read_y;
			buffer.read_normalized_vector2(read_x, read_y, compression_level);
			const bool is_equal = NS::MathFunc::is_equal_approx<T>(read_x, value.first, epsilon) && NS::MathFunc::is_equal_approx(read_y, value.second, epsilon);
			NS_ASSERT_COND(!buffer.is_buffer_failed());
			NS_ASSERT_COND_MSG(is_equal, "Should read the same value.");
		}
	}
}

template <typename T>
void test_data_buffer_vector_3() {
	for (int cl = (int)NS::DataBuffer::COMPRESSION_LEVEL_0; cl <= (int)NS::DataBuffer::COMPRESSION_LEVEL_3; cl++) {
		const NS::DataBuffer::CompressionLevel compression_level = (NS::DataBuffer::CompressionLevel)cl;
		const T epsilon = NS::DataBuffer::get_real_epsilon<T>(NS::DataBuffer::DATA_TYPE_VECTOR3, compression_level);

		const std::vector<std::tuple<T, T, T>> values = vector_3_values<T>(compression_level);

		for (int i = 0; i < values.size(); ++i) {
			NS::DataBuffer buffer;
			buffer.begin_write(debugger, 0);

			const std::tuple<T, T, T> value = values[i];

			buffer.add_vector3(std::get<0>(value), std::get<1>(value), std::get<2>(value), compression_level);
			if (std::is_same<T, float>::value && compression_level == NS::DataBuffer::COMPRESSION_LEVEL_0) {
				// Fallback to compression level 1
				NS_ASSERT_COND(buffer.get_bit_offset() == buffer.get_bit_taken(NS::DataBuffer::DATA_TYPE_VECTOR3, NS::DataBuffer::COMPRESSION_LEVEL_1));
			} else {
				NS_ASSERT_COND(buffer.get_bit_offset() == buffer.get_bit_taken(NS::DataBuffer::DATA_TYPE_VECTOR3, compression_level));
			}
			NS_ASSERT_COND(!buffer.is_buffer_failed());

			buffer.begin_read(debugger);
			T read_x;
			T read_y;
			T read_z;
			buffer.read_vector3(read_x, read_y, read_z, compression_level);
			const bool is_equal =
					NS::MathFunc::is_equal_approx<T>(read_x, std::get<0>(value), epsilon) && NS::MathFunc::is_equal_approx(read_y, std::get<1>(value), epsilon) && NS::MathFunc::is_equal_approx(read_z, std::get<2>(value), epsilon);
			NS_ASSERT_COND(!buffer.is_buffer_failed());
			NS_ASSERT_COND_MSG(is_equal, "Should read the same value.");
		}
	}
}

template <typename T>
void test_data_buffer_normalized_vector_3() {
	for (int cl = (int)NS::DataBuffer::COMPRESSION_LEVEL_0; cl <= (int)NS::DataBuffer::COMPRESSION_LEVEL_3; cl++) {
		const NS::DataBuffer::CompressionLevel compression_level = (NS::DataBuffer::CompressionLevel)cl;
		const T epsilon = NS::DataBuffer::get_real_epsilon<T>(NS::DataBuffer::DATA_TYPE_NORMALIZED_VECTOR3, compression_level);

		const std::vector<std::tuple<T, T, T>> values = normalized_vector_3_values<T>(compression_level);

		for (int i = 0; i < values.size(); ++i) {
			NS::DataBuffer buffer;
			buffer.begin_write(debugger, 0);

			const std::tuple<T, T, T> value = values[i];

			buffer.add_normalized_vector3(std::get<0>(value), std::get<1>(value), std::get<2>(value), compression_level);
			NS_ASSERT_COND(!buffer.is_buffer_failed());
			NS_ASSERT_COND(buffer.get_bit_offset() == buffer.get_bit_taken(NS::DataBuffer::DATA_TYPE_NORMALIZED_VECTOR3, compression_level));

			buffer.begin_read(debugger);
			T read_x;
			T read_y;
			T read_z;
			buffer.read_normalized_vector3(read_x, read_y, read_z, compression_level);
			const bool is_equal = NS::MathFunc::is_equal_approx<T>(read_x, std::get<0>(value), epsilon) && NS::MathFunc::is_equal_approx(read_y, std::get<1>(value), epsilon) && NS::MathFunc::is_equal_approx(read_z, std::get<2>(value), epsilon);
			NS_ASSERT_COND(!buffer.is_buffer_failed());
			NS_ASSERT_COND_MSG(is_equal, "Should read the same value.");
		}
	}
}

void test_data_buffer_bits() {
	NS::DataBuffer buffer;
	buffer.begin_write(debugger, 0);

	buffer.add_bool(false);
	buffer.add_bool(true);
	buffer.add_bool(true);
	NS_ASSERT_COND(!buffer.is_buffer_failed());

	const std::vector<uint8_t> bytes = byte_values();
	buffer.add_bits(bytes.data(), int(bytes.size() * 8));
	NS_ASSERT_COND(!buffer.is_buffer_failed());

	buffer.begin_read(debugger);
	buffer.read_bool();
	buffer.read_bool();
	buffer.read_bool();
	NS_ASSERT_COND(!buffer.is_buffer_failed());

	std::vector<uint8_t> read_bytes;
	read_bytes.resize(bytes.size());
	buffer.read_bits(read_bytes.data(), int(bytes.size() * 8));
	NS_ASSERT_COND(!buffer.is_buffer_failed());

	NS_ASSERT_COND(std::equal(bytes.begin(), bytes.end(), read_bytes.begin()));
}

void test_data_buffer_data_buffer() {
	NS::DataBuffer main_buffer;
	main_buffer.begin_write(debugger, 0);

	const std::vector<uint8_t> bytes = byte_values();

	{
		NS::DataBuffer buffer;
		buffer.begin_write(debugger, 0);

		buffer.add_bool(false);
		buffer.add_bool(true);
		buffer.add_bool(true);
		NS_ASSERT_COND(!buffer.is_buffer_failed());

		buffer.add_bits(bytes.data(), int(bytes.size() * 8));
		NS_ASSERT_COND(!buffer.is_buffer_failed());

		main_buffer.add_data_buffer(buffer);
		NS_ASSERT_COND(!main_buffer.is_buffer_failed());
	}

	{
		NS::DataBuffer buffer;
		buffer.begin_write(debugger, 0);

		main_buffer.begin_read(debugger);
		main_buffer.read_data_buffer(buffer);
		NS_ASSERT_COND(!main_buffer.is_buffer_failed());

		buffer.begin_read(debugger);
		buffer.read_bool();
		buffer.read_bool();
		buffer.read_bool();
		NS_ASSERT_COND(!buffer.is_buffer_failed());

		std::vector<uint8_t> read_bytes;
		read_bytes.resize(bytes.size());
		buffer.read_bits(read_bytes.data(), int(bytes.size() * 8));
		NS_ASSERT_COND(!buffer.is_buffer_failed());

		NS_ASSERT_COND(std::equal(bytes.begin(), bytes.end(), read_bytes.begin()));
	}
}

void test_data_buffer_seek() {
	NS::DataBuffer buffer;
	buffer.begin_write(debugger, 0);
	buffer.add_bool(true);
	buffer.add_bool(false);

	buffer.seek(-1);
	NS_ASSERT_COND_MSG(buffer.get_bit_offset() == 2, "Bit offset should fail for negative values");

	buffer.begin_read(debugger);
	NS_ASSERT_COND(buffer.get_bit_offset() == 0);

	buffer.seek(1);
	NS_ASSERT_COND_MSG(buffer.get_bit_offset() == 1, "Bit offset should be 1 after seek to 1");
	NS_ASSERT_COND_MSG(buffer.read_bool() == false, "Should read false at position 1");

	buffer.seek(0);
	NS_ASSERT_COND_MSG(buffer.get_bit_offset() == 0, "Bit offset should be 0 after seek to 0");
	NS_ASSERT_COND_MSG(buffer.read_bool() == true, "Should read true at position 0");
}

void test_data_buffer_metadata() {
	bool metadata[] = { true, false };
	bool value[] = { false, true };

	for (int i = 0; i < 2; i++) {
		NS::DataBuffer buffer;
		const int metadata_size = buffer.get_bit_taken(NS::DataBuffer::DATA_TYPE_BOOL, NS::DataBuffer::COMPRESSION_LEVEL_0);
		buffer.begin_write(debugger, metadata_size);
		buffer.add_bool(metadata[i]);
		buffer.add_bool(value[i]);
		buffer.begin_read(debugger);
		NS_ASSERT_COND_MSG(buffer.read_bool() == metadata[i], "Should return correct metadata");
		NS_ASSERT_COND_MSG(buffer.read_bool() == value[i], "Should return correct value after metadata");
		NS_ASSERT_COND_MSG(buffer.get_metadata_size() == metadata_size, "Metadata size should be equal to expected");
		NS_ASSERT_COND_MSG(buffer.size() == buffer.get_bit_taken(NS::DataBuffer::DATA_TYPE_BOOL, NS::DataBuffer::COMPRESSION_LEVEL_0), "Size should be equal to expected");
		NS_ASSERT_COND_MSG(buffer.total_size() == buffer.get_bit_taken(NS::DataBuffer::DATA_TYPE_BOOL, NS::DataBuffer::COMPRESSION_LEVEL_0) + metadata_size, "Total size should be equal to expected");
	}
}

void test_data_buffer_zero() {
	constexpr NS::DataBuffer::CompressionLevel compression = NS::DataBuffer::COMPRESSION_LEVEL_0;
	NS::DataBuffer buffer;
	buffer.begin_write(debugger, 0);
	buffer.add_int(-1, compression);
	buffer.zero();
	buffer.begin_read(debugger);
	NS_ASSERT_COND_MSG(buffer.read_int(compression) == 0, "Should return 0");
}

void test_data_buffer_shrinking() {
	NS::DataBuffer buffer;
	buffer.begin_write(debugger, 0);
	for (int i = 0; i < 2; ++i) {
		buffer.add_real(3.14, NS::DataBuffer::COMPRESSION_LEVEL_0);
	}
	const int original_size = buffer.total_size();

	buffer.shrink_to(0, original_size + 1);
	NS_ASSERT_COND_MSG(buffer.total_size() == original_size, "Shrinking to a larger size should fail.");

	buffer.shrink_to(0, original_size - 8);
	NS_ASSERT_COND_MSG(buffer.total_size() == original_size - 8, "Shrinking by 1 byte should succeed.");
	NS_ASSERT_COND_MSG(buffer.get_buffer().size_in_bits() == original_size, "Buffer size after shrinking by 1 byte should be the same.");

	buffer.dry();
	NS_ASSERT_COND_MSG(buffer.get_buffer().size_in_bits() == original_size - 8, "Buffer size after dry should changed to the smallest posiible.");
}

void test_data_buffer_skip() {
	const bool value = true;

	NS::DataBuffer buffer;
	buffer.begin_write(debugger, 0);
	buffer.add_bool(!value);
	buffer.add_bool(value);

	buffer.begin_read(debugger);
	buffer.seek(buffer.get_bit_taken(NS::DataBuffer::DATA_TYPE_BOOL, NS::DataBuffer::COMPRESSION_LEVEL_0));
	NS_ASSERT_COND_MSG(buffer.read_bool() == value, "Should read the same value");
}

void test_data_buffer_writing_failing() {
	{
		NS::DataBuffer buffer;
		buffer.begin_read(debugger);
		NS_ASSERT_COND(!buffer.is_buffer_failed());
		buffer.add_bool(true);
		NS_ASSERT_COND(buffer.is_buffer_failed());
	}
	{
		NS::DataBuffer buffer;
		buffer.begin_read(debugger);
		NS_ASSERT_COND(!buffer.is_buffer_failed());
		buffer.add_int(1, NS::DataBuffer::COMPRESSION_LEVEL_0);
		NS_ASSERT_COND(buffer.is_buffer_failed());
	}
	{
		NS::DataBuffer buffer;
		buffer.begin_read(debugger);
		NS_ASSERT_COND(!buffer.is_buffer_failed());
		buffer.add_uint(1, NS::DataBuffer::COMPRESSION_LEVEL_0);
		NS_ASSERT_COND(buffer.is_buffer_failed());
	}
	{
		NS::DataBuffer buffer;
		buffer.begin_read(debugger);
		NS_ASSERT_COND(!buffer.is_buffer_failed());
		buffer.add_normalized_vector2(0.f, 0.f, NS::DataBuffer::COMPRESSION_LEVEL_0);
		NS_ASSERT_COND(buffer.is_buffer_failed());
	}
	{
		NS::DataBuffer buffer;
		buffer.begin_read(debugger);
		NS_ASSERT_COND(!buffer.is_buffer_failed());
		buffer.add_normalized_vector3(0.f, 0.f, 0.f, NS::DataBuffer::COMPRESSION_LEVEL_0);
		NS_ASSERT_COND(buffer.is_buffer_failed());
	}
}

void test_data_buffer_reading_failing() {
	{
		NS::DataBuffer buffer;
		buffer.begin_write(debugger, 0);
		NS_ASSERT_COND(!buffer.is_buffer_failed());
		buffer.read_bool();
		NS_ASSERT_COND(buffer.is_buffer_failed());
	}
	{
		NS::DataBuffer buffer;
		buffer.begin_write(debugger, 0);
		NS_ASSERT_COND(!buffer.is_buffer_failed());
		buffer.read_int(NS::DataBuffer::COMPRESSION_LEVEL_0);
		NS_ASSERT_COND(buffer.is_buffer_failed());
	}
	{
		NS::DataBuffer buffer;
		buffer.begin_write(debugger, 0);
		NS_ASSERT_COND(!buffer.is_buffer_failed());
		buffer.read_uint(NS::DataBuffer::COMPRESSION_LEVEL_0);
		NS_ASSERT_COND(buffer.is_buffer_failed());
	}
	{
		NS::DataBuffer buffer;
		buffer.begin_write(debugger, 0);
		NS_ASSERT_COND(!buffer.is_buffer_failed());
		double x, y;
		buffer.read_normalized_vector2(x, y, NS::DataBuffer::COMPRESSION_LEVEL_0);
		NS_ASSERT_COND(buffer.is_buffer_failed());
	}
	{
		NS::DataBuffer buffer;
		buffer.begin_write(debugger, 0);
		NS_ASSERT_COND(!buffer.is_buffer_failed());
		double x, y, z;
		buffer.read_normalized_vector3(x, y, z, NS::DataBuffer::COMPRESSION_LEVEL_0);
		NS_ASSERT_COND(buffer.is_buffer_failed());
	}
}

void test_data_buffer_slice_copy() {
	NS::DataBuffer origin_buffer;

	const int first_integer = 12931237123123;
	const int second_integer = 1998237123123;

	origin_buffer.begin_write(debugger, 0);
	origin_buffer.add(true);
	origin_buffer.add(false);
	origin_buffer.add(first_integer);
	origin_buffer.add(true);
	origin_buffer.add(false);

	NS_ASSERT_COND(!origin_buffer.is_buffer_failed());
	const int current_offset = origin_buffer.get_bit_offset();

	{
		NS::DataBuffer slice;
		slice.begin_write(debugger, 0);
		NS_ASSERT_COND(origin_buffer.slice(slice, 0, 2));
		NS_ASSERT_COND(!origin_buffer.is_buffer_failed());
		NS_ASSERT_COND(origin_buffer.get_bit_offset()==current_offset);

		slice.begin_read(debugger);
		NS_ASSERT_COND(slice.read_bool()== true);
		NS_ASSERT_COND(slice.read_bool()== false);
	}

	{
		NS::DataBuffer slice;
		slice.begin_write(debugger, 0);
		NS_ASSERT_COND(origin_buffer.slice(slice, 2, 32));
		NS_ASSERT_COND(!origin_buffer.is_buffer_failed());
		NS_ASSERT_COND(origin_buffer.get_bit_offset()==current_offset);

		slice.begin_read(debugger);
		NS_ASSERT_COND(slice.read_int(NS::DataBuffer::COMPRESSION_LEVEL_1)== first_integer);
	}

	{
		NS::DataBuffer slice;
		slice.begin_write(debugger, 0);
		NS_ASSERT_COND(origin_buffer.slice(slice, 34, 2));
		NS_ASSERT_COND(!origin_buffer.is_buffer_failed());
		NS_ASSERT_COND(origin_buffer.get_bit_offset()==current_offset);

		slice.begin_read(debugger);
		NS_ASSERT_COND(slice.read_bool()== true);
		NS_ASSERT_COND(slice.read_bool()== false);
	}

	{
		NS::DataBuffer slice;
		slice.begin_write(debugger, 0);
		slice.add(true);
		slice.add(false);
		slice.add(second_integer);

		NS_ASSERT_COND(origin_buffer.slice(slice, 2, 34));
		NS_ASSERT_COND(!origin_buffer.is_buffer_failed());
		NS_ASSERT_COND(origin_buffer.get_bit_offset()==current_offset);

		slice.begin_read(debugger);
		NS_ASSERT_COND(slice.read_bool()== true);
		NS_ASSERT_COND(slice.read_bool()== false);
		NS_ASSERT_COND(slice.read_int(NS::DataBuffer::COMPRESSION_LEVEL_1)== second_integer);
		NS_ASSERT_COND(slice.read_int(NS::DataBuffer::COMPRESSION_LEVEL_1)== first_integer);
		NS_ASSERT_COND(slice.read_bool()== true);
		NS_ASSERT_COND(slice.read_bool()== false);
	}

	origin_buffer.add(false);
	origin_buffer.add(false);
	origin_buffer.add(false);
	origin_buffer.add(false);
	origin_buffer.add(std::uint8_t(254));

	{
		NS::DataBuffer slice;
		slice.begin_write(debugger, 0);
		NS_ASSERT_COND(origin_buffer.slice(slice, 40, 8));
		NS_ASSERT_COND(!origin_buffer.is_buffer_failed());
		NS_ASSERT_COND(origin_buffer.get_bit_offset()==current_offset + 12);

		slice.begin_read(debugger);
		NS_ASSERT_COND(slice.read_uint(NS::DataBuffer::COMPRESSION_LEVEL_3)== 254);
	}
}

void test_data_buffer_compare() {
	NS::DataBuffer first_buffer;
	first_buffer.begin_write(debugger, 0);
	first_buffer.add(true);
	first_buffer.add(false);
	first_buffer.add(12931237123123);

	NS::DataBuffer second_buffer;
	second_buffer.begin_write(debugger, 0);
	second_buffer.add(true);
	second_buffer.add(false);
	second_buffer.add(12931237123123);

	NS_ASSERT_COND(first_buffer == second_buffer);

	second_buffer.seek(0);
	second_buffer.add(false);
	NS_ASSERT_COND(first_buffer != second_buffer);

	second_buffer.seek(0);
	second_buffer.add(true);
	NS_ASSERT_COND(first_buffer == second_buffer);

	second_buffer.seek(first_buffer.get_bit_offset());
	NS_ASSERT_COND(first_buffer.get_bit_offset() == second_buffer.get_bit_offset());

	first_buffer.add(true);
	first_buffer.add(false);

	second_buffer.add(false);
	second_buffer.add(false);
	second_buffer.add(false);
	second_buffer.add(false);
	second_buffer.add(true);
	second_buffer.add(true);

	NS_ASSERT_COND(first_buffer != second_buffer);

	// Since the buffers are the same for the first part, this can't fail.
	first_buffer.shrink_to(0, first_buffer.get_size_in_bits() - 2);
	second_buffer.shrink_to(0, first_buffer.get_size_in_bits());
	NS_ASSERT_COND(first_buffer == second_buffer);
}

void NS_Test::test_data_buffer() {
	test_data_buffer_string();
	test_data_buffer_u16string();
	test_data_buffer_bool();
	test_data_buffer_int();
	test_data_buffer_uint();
	test_data_buffer_real<double>();
	test_data_buffer_real<float>();
	test_data_buffer_positive_unit_real<float>();
	test_data_buffer_unit_real<float>();
	test_data_buffer_vector_2<double>();
	test_data_buffer_vector_2<float>();
	test_data_buffer_vector_3<double>();
	test_data_buffer_vector_3<float>();
	test_data_buffer_normalized_vector_2<float>();
	test_data_buffer_normalized_vector_2<double>();
	test_data_buffer_normalized_vector_3<float>();
	test_data_buffer_normalized_vector_3<double>();
	test_data_buffer_bits();
	test_data_buffer_data_buffer();
	test_data_buffer_seek();
	test_data_buffer_metadata();
	test_data_buffer_zero();
	test_data_buffer_shrinking();
	test_data_buffer_skip();
	test_data_buffer_writing_failing();
	test_data_buffer_reading_failing();
	test_data_buffer_unaligned_write_read();
	test_data_buffer_slice_copy();
	test_data_buffer_compare();
}