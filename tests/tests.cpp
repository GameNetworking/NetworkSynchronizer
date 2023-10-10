#include "tests.h"

#include "local_network.h"
#include "test_processor.h"
#include "test_scene_synchronizer.h"

void NS_Test::test_all() {
	// TODO test DataBuffer.
	test_processor();
	test_local_network();
	test_scene_synchronizer();
}