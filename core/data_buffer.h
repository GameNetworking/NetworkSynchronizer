#pragma once

#include "bit_array.h"
#include "core.h"
#include <string>

NS_NAMESPACE_BEGIN
class DataBuffer {
public:
	enum DataType {
		DATA_TYPE_BOOL,
		DATA_TYPE_INT,
		DATA_TYPE_UINT,
		DATA_TYPE_REAL,
		DATA_TYPE_POSITIVE_UNIT_REAL,
		DATA_TYPE_UNIT_REAL,
		DATA_TYPE_VECTOR2,
		DATA_TYPE_NORMALIZED_VECTOR2,
		DATA_TYPE_VECTOR3,
		DATA_TYPE_NORMALIZED_VECTOR3,
		DATA_TYPE_BITS,
		// The only dynamic sized value.
		DATA_TYPE_DATABUFFER
	};

	/// Compression level for the stored input data.
	///
	/// Depending on the data type and the compression level used the amount of
	/// bits used and loss change.
	///
	///
	/// ## Bool
	/// Always use 1 bit
	///
	///
	/// ## Int
	/// COMPRESSION_LEVEL_0: 64 bits are used - Stores integers -9223372036854775808 / 9223372036854775807
	/// COMPRESSION_LEVEL_1: 32 bits are used - Stores integers -2147483648 / 2147483647
	/// COMPRESSION_LEVEL_2: 16 bits are used - Stores integers -32768 / 32767
	/// COMPRESSION_LEVEL_3: 8 bits are used - Stores integers -128 / 127
	///
	///
	/// ## Uint
	/// COMPRESSION_LEVEL_0: 64 bits are used - Stores integers 18446744073709551615
	/// COMPRESSION_LEVEL_1: 32 bits are used - Stores integers 4294967295
	/// COMPRESSION_LEVEL_2: 16 bits are used - Stores integers 65535
	/// COMPRESSION_LEVEL_3: 8 bits are used - Stores integers 2SS
	///
	///
	/// ## Real
	/// Precision depends on an integer range
	/// COMPRESSION_LEVEL_0: 64 bits are used - Double precision. Up to 16 precision is 0.00000000000000177636 in worst case. Up to 512 precision is 0.00000000000005684342 in worst case. Up to 1024 precision is 0.00000000000011368684 in worst case.
	/// COMPRESSION_LEVEL_1: 32 bits are used - Single precision (float). Up to 16 precision is 0.00000095367431640625 in worst case. Up to 512 precision is 0.000030517578125 in worst case. Up to 1024 precision is 0.00006103515625 in worst case.
	/// COMPRESSION_LEVEL_2: 16 bits are used - Half precision. Up to 16 precision is 0.0078125 in worst case. Up to 512 precision is 0.25 in worst case. Up to 1024 precision is 0.5.
	/// COMPRESSION_LEVEL_3:                  - Fallbacks to level 2.
	///
	/// NOTE: User get_real_precision to get the epsilon for each precision according to the compression level.
	///
	///
	/// ## Positive unit real
	/// COMPRESSION_LEVEL_0: 10 bits are used - Max loss ~0.005%
	/// COMPRESSION_LEVEL_1: 8 bits are used - Max loss ~0.020%
	/// COMPRESSION_LEVEL_2: 6 bits are used - Max loss ~0.793%
	/// COMPRESSION_LEVEL_3: 4 bits are used - Max loss ~3.333%
	///
	///
	/// ## Unit real (uses one extra bit for the sign)
	/// COMPRESSION_LEVEL_0: 11 bits are used - Max loss ~0.005%
	/// COMPRESSION_LEVEL_1: 9 bits are used - Max loss ~0.020%
	/// COMPRESSION_LEVEL_2: 7 bits are used - Max loss ~0.793%
	/// COMPRESSION_LEVEL_3: 5 bits are used - Max loss ~3.333%
	///
	///
	/// ## Vector2
	/// COMPRESSION_LEVEL_0: 2 * 64 bits are used - Double precision
	/// COMPRESSION_LEVEL_1: 2 * 32 bits are used - Single precision
	/// COMPRESSION_LEVEL_2: 2 * 16 bits are used - Half precision
	/// COMPRESSION_LEVEL_3: 2 * 8 bits are used - Minifloat
	///
	/// For floating point precision, check the Real compression section.
	///
	///
	/// ## Normalized Vector2
	/// COMPRESSION_LEVEL_0: 12 bits are used - Max loss 0.17°
	/// COMPRESSION_LEVEL_1: 11 bits are used - Max loss 0.35°
	/// COMPRESSION_LEVEL_2: 10 bits are used - Max loss 0.7°
	/// COMPRESSION_LEVEL_3: 9 bits are used - Max loss 1.1°
	///
	///
	/// ## Vector3
	/// COMPRESSION_LEVEL_0: 3 * 64 bits are used - Double precision
	/// COMPRESSION_LEVEL_1: 3 * 32 bits are used - Single precision
	/// COMPRESSION_LEVEL_2: 3 * 16 bits are used - Half precision
	/// COMPRESSION_LEVEL_3: 3 * 8 bits are used - Minifloat
	///
	/// For floating point precision, check the Real compression section.
	///
	///
	/// ## Normalized Vector3
	/// COMPRESSION_LEVEL_0: 11 * 3 bits are used - Max loss ~0.005% per axis
	/// COMPRESSION_LEVEL_1: 9 * 3 bits are used - Max loss ~0.020% per axis
	/// COMPRESSION_LEVEL_2: 7 * 3 bits are used - Max loss ~0.793% per axis
	/// COMPRESSION_LEVEL_3: 5 * 3 bits are used - Max loss ~3.333% per axis
	///
	/// ## Variant
	/// It's dynamic sized. It's not possible to compress it.
	enum CompressionLevel {
		COMPRESSION_LEVEL_0 = 0,
		COMPRESSION_LEVEL_1 = 1,
		COMPRESSION_LEVEL_2 = 2,
		COMPRESSION_LEVEL_3 = 3
	};

private:
	class SceneSynchronizerDebugger *debugger = nullptr;
	int metadata_size = 0;
	int bit_offset = 0;
	int bit_size = 0;
	bool is_reading = false;
	BitArray buffer;

	bool buffer_failed = false;

#ifdef NS_DEBUG_ENABLED
	bool debug_enabled = true;
#endif

public:
	DataBuffer() = default; // Use this with care: Not initialising the debugger will cause a crash.
	DataBuffer(class SceneSynchronizerDebugger &p_debugger);
	DataBuffer(class SceneSynchronizerDebugger &p_debugger, const DataBuffer &p_other);
	DataBuffer(class SceneSynchronizerDebugger &p_debugger, const BitArray &p_buffer);

	class SceneSynchronizerDebugger &get_debugger() const {
		return *debugger;
	}

	//DataBuffer &operator=(DataBuffer &&p_other);
	bool operator==(const DataBuffer &p_other) const;

	void copy(const DataBuffer &p_other);
	void copy(const BitArray &p_buffer);

	const BitArray &get_buffer() const {
		return buffer;
	}

	BitArray &get_buffer_mut() {
		return buffer;
	}

	/// Begin write.
	void begin_write(int p_metadata_size);

	/// Make sure the buffer takes least space possible.
	void dry();

	/// Seek the offset to a specific bit. Seek to a bit greater than the actual
	/// size is not allowed.
	void seek(int p_bits);

	/// Set the bit size and the metadata size.
	void shrink_to(int p_metadata_bit_size, int p_bit_size);

	/// Returns the metadata size in bits.
	int get_metadata_size() const;
	/// Returns the buffer size in bits
	int size() const;
	/// Total size in bits.
	int total_size() const;

	/// Returns the bit offset.
	int get_bit_offset() const;

	/// Skip n bits.
	void skip(int p_bits);

	/// Begin read.
	void begin_read();

	bool is_buffer_failed() const {
		return buffer_failed;
	}

	// ------------------------------------------------------ Type serialization
	void add(bool p_input);
	void read(bool &p_out);

	void add(std::uint8_t p_input);
	void read(std::uint8_t &r_out);

	void add(std::uint16_t p_input);
	void read(std::uint16_t &r_out);

	void add(std::uint32_t p_input);
	void read(std::uint32_t &r_out);

	void add(std::uint64_t p_input);
	void read(std::uint64_t &r_out);

	void add(std::int8_t p_input);
	void read(std::int8_t &r_out);

	void add(std::int16_t p_input);
	void read(std::int16_t &r_out);

	void add(std::int32_t p_input);
	void read(std::int32_t &r_out);

	void add(std::int64_t p_input);
	void read(std::int64_t &r_out);

	void add(float p_input);
	void read(float &p_out);

	void add(double p_input);
	void read(double &p_out);

	void add(const std::string &p_string);
	void read(std::string &r_out);

	void add(const std::u16string &p_string);
	void read(std::u16string &r_out);

	void add(const DataBuffer &p_db);
	void read(DataBuffer &r_db);

	// -------------------------------------------------- Specific serialization

	/// Add a boolean to the buffer.
	/// Returns the same data.
	bool add_bool(bool p_input);

	/// Parse the next data as boolean.
	bool read_bool();

	/// Add the next data as int.
	std::int64_t add_int(std::int64_t p_input, CompressionLevel p_compression_level);

	/// Parse the next data as int.
	std::int64_t read_int(CompressionLevel p_compression_level);

	/// Add the next data as uint
	std::uint64_t add_uint(std::uint64_t p_input, CompressionLevel p_compression_level);

	/// Parse the next data as uint.
	std::uint64_t read_uint(CompressionLevel p_compression_level);

	/// Add a real into the buffer. Depending on the compression level is possible
	/// to store different range level.
	/// The fractional part has a precision of ~0.3%
	///
	/// Returns the compressed value so both the client and the peers can use
	/// the same data.
	void add_real(double p_input, CompressionLevel p_compression_level);
	void add_real(float p_input, CompressionLevel p_compression_level);

	/// Parse the following data as a real.
	void read_real(double &r_value, CompressionLevel p_compression_level);
	void read_real(float &r_value, CompressionLevel p_compression_level);

	/// Add a positive unit real into the buffer.
	///
	/// **Note:** Not unitary values lead to unexpected behaviour.
	///
	/// Returns the compressed value so both the client and the peers can use
	/// the same data.
	float add_positive_unit_real(float p_input, CompressionLevel p_compression_level);

	/// Parse the following data as a positive unit real.
	float read_positive_unit_real(CompressionLevel p_compression_level);

	/// Add a unit real into the buffer.
	///
	/// **Note:** Not unitary values lead to unexpected behaviour.
	///
	/// Returns the compressed value so both the client and the peers can use
	/// the same data.
	float add_unit_real(float p_input, CompressionLevel p_compression_level);

	/// Parse the following data as an unit real.
	float read_unit_real(CompressionLevel p_compression_level);

	/// Add a vector2 into the buffer.
	/// Note: This kind of vector occupies more space than the normalized verison.
	/// Consider use a normalized vector to save bandwidth if possible.
	///
	/// Returns the decompressed vector so both the client and the peers can use
	/// the same data.
	void add_vector2(double x, double y, CompressionLevel p_compression_level);
	void add_vector2(float x, float y, CompressionLevel p_compression_level);

	/// Parse next data as vector from the input buffer.
	void read_vector2(double &x, double &y, CompressionLevel p_compression_level);
	void read_vector2(float &x, float &y, CompressionLevel p_compression_level);

	/// Add a normalized vector2 into the buffer.
	/// Note: The compression algorithm rely on the fact that this is a
	/// normalized vector. The behaviour is unexpected for not normalized vectors.
	///
	/// Returns the decompressed vector so both the client and the peers can use
	/// the same data.
	template <typename T>
	void add_normalized_vector2(T x, T y, CompressionLevel p_compression_level);

	/// Parse next data as normalized vector from the input buffer.
	template <typename T>
	void read_normalized_vector2(T &x, T &y, CompressionLevel p_compression_level);

	/// Add a vector3 into the buffer.
	/// Note: This kind of vector occupies more space than the normalized verison.
	/// Consider use a normalized vector to save bandwidth if possible.
	///
	/// Returns the decompressed vector so both the client and the peers can use
	/// the same data.
	void add_vector3(double x, double y, double z, CompressionLevel p_compression_level);
	void add_vector3(float x, float y, float z, CompressionLevel p_compression_level);

	/// Parse next data as vector3 from the input buffer.
	void read_vector3(double &x, double &y, double &z, CompressionLevel p_compression_level);
	void read_vector3(float &x, float &y, float &z, CompressionLevel p_compression_level);

	/// Add a normalized vector3 into the buffer.
	/// Note: The compression algorithm rely on the fact that this is a
	/// normalized vector. The behaviour is unexpected for not normalized vectors.
	///
	/// Returns the decompressed vector so both the client and the peers can use
	/// the same data.
	template <typename T>
	void add_normalized_vector3(T x, T y, T z, CompressionLevel p_compression_level);

	/// Parse next data as normalized vector3 from the input buffer.
	template <typename T>
	void read_normalized_vector3(T &x, T &y, T &z, CompressionLevel p_compression_level);

	/// Add a data buffer to this buffer.
	void add_data_buffer(const DataBuffer &p_db);
	void read_data_buffer(DataBuffer &r_db);

	/// Add bits of custom size.
	void add_bits(const uint8_t *p_data, int p_bit_count);
	void read_bits(uint8_t *r_data, int p_bit_count);

	/// Puts all the bytes to 0.
	void zero();

	/** Skips the amount of bits a type takes. */

	void skip_bool();
	void skip_int(CompressionLevel p_compression);
	void skip_uint(CompressionLevel p_compression);
	void skip_real(CompressionLevel p_compression);
	void skip_positive_unit_real(CompressionLevel p_compression);
	void skip_unit_real(CompressionLevel p_compression);
	void skip_vector2(CompressionLevel p_compression);
	void skip_normalized_vector2(CompressionLevel p_compression);
	void skip_vector3(CompressionLevel p_compression);
	void skip_normalized_vector3(CompressionLevel p_compression);
	void skip_buffer();

	/** Just returns the size of a specific type. */

	int get_bool_size() const;
	int get_int_size(CompressionLevel p_compression) const;
	int get_uint_size(CompressionLevel p_compression) const;
	int get_real_size(CompressionLevel p_compression) const;
	int get_positive_unit_real_size(CompressionLevel p_compression) const;
	int get_unit_real_size(CompressionLevel p_compression) const;
	int get_vector2_size(CompressionLevel p_compression) const;
	int get_normalized_vector2_size(CompressionLevel p_compression) const;
	int get_vector3_size(CompressionLevel p_compression) const;
	int get_normalized_vector3_size(CompressionLevel p_compression) const;

	/** Read the size and pass to the next parameter. */

	int read_bool_size();
	int read_int_size(CompressionLevel p_compression);
	int read_uint_size(CompressionLevel p_compression);
	int read_real_size(CompressionLevel p_compression);
	int read_positive_unit_real_size(CompressionLevel p_compression);
	int read_unit_real_size(CompressionLevel p_compression);
	int read_vector2_size(CompressionLevel p_compression);
	int read_normalized_vector2_size(CompressionLevel p_compression);
	int read_vector3_size(CompressionLevel p_compression);
	int read_normalized_vector3_size(CompressionLevel p_compression);
	int read_buffer_size();

	int get_bit_taken(DataType p_data_type, CompressionLevel p_compression) const;
	template <typename T>
	static T get_real_epsilon(DataType p_data_type, CompressionLevel p_compression);

public: // ---------------------------------------------------------------- Internal
	template <typename T>
	static uint64_t compress_unit_float(T p_value, T p_scale_factor);
	template <typename T>
	static T decompress_unit_float(uint64_t p_value, T p_scale_factor);

	void make_room_in_bits(int p_dim);
	void make_room_pad_to_next_byte();
	bool pad_to_next_byte(int *p_bits_to_next_byte = nullptr);
};

NS_NAMESPACE_END