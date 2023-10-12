#include "test_data_buffer.h"

#include "../data_buffer.h"

void test_data_buffer_string() {
	DataBuffer db;
	db.begin_write(0);

	std::string abc_1("abc_1");
	db.add(abc_1);

	db.begin_read();
	std::string abc_1_r;
	db.read(abc_1_r);

	CRASH_COND(abc_1 != abc_1_r);
}

void test_data_buffer_u16string() {
	{
		DataBuffer db;
		db.begin_write(0);

		std::u16string abc_1(u"abc_1");
		db.add(abc_1);

		std::string abc_2("abc_2");
		db.add(abc_2);

		std::u16string abc_3(u"abc_3");
		db.add(abc_3);

		db.begin_read();
		std::u16string abc_1_r;
		db.read(abc_1_r);

		std::string abc_2_r;
		db.read(abc_2_r);

		std::u16string abc_3_r;
		db.read(abc_3_r);

		CRASH_COND(abc_1 != abc_1_r);
		CRASH_COND(abc_2 != abc_2_r);
		CRASH_COND(abc_3 != abc_3_r);
	}
}

void NS_Test::test_data_buffer() {
	test_data_buffer_string();
	test_data_buffer_u16string();
}
