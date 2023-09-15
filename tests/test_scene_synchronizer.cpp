
#include "core/error/error_macros.h"
#include "core/os/main_loop.h"
#include "core/os/memory.h"
#include "editor/editor_node.h"
#include "modules/network_synchronizer/networked_controller.h"
#include "modules/network_synchronizer/scene_synchronizer.h"
#include "modules/network_synchronizer/snapshot.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"

SceneTree *st = nullptr;

template <class C>
C *new_node(const StringName &p_name, Node *p_parent) {
	C *n = memnew(C);
	n->set_name(p_name);

	CRASH_COND_MSG(n->is_ready(), "The node should NOT be ready at this point.");
	p_parent->add_child(n);
	CRASH_COND_MSG(!n->is_ready(), "The node should be ready at this point.");

	return n;
}

void delete_node(Node *p_node) {
	if (p_node->get_parent()) {
		p_node->get_parent()->remove_child(p_node);
	}
	memdelete(p_node);
}

void test_scene_node_registration() {
	st->reload_current_scene();

	SceneSynchronizer *synchronizer = new_node<SceneSynchronizer>("scene_sync", st->get_root());
	Node *node = new_node<Node>("node", st->get_root());

	synchronizer->register_variable(node, "global_transform");

	delete_node(node);
	delete_node(synchronizer);
}

void test_scene_synchronizer() {
	st = memnew(SceneTree);

	test_scene_node_registration();

	memdelete(st);
	st = nullptr;
}