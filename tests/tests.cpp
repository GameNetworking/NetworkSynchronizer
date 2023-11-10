#include "tests.h"

#include "local_scene.h"
#include "test_data_buffer.h"
#include "test_processor.h"
#include "test_scene_synchronizer.h"
#include "test_simulation.h"

void NS_Test::test_all() {
	NS::LocalSceneSynchronizer::register_local_sync();
	// TODO test DataBuffer.
	test_data_buffer();
	test_processor();
	test_local_network();
	test_scene_synchronizer();
	test_simulation();
}