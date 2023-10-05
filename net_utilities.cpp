/*************************************************************************/
/*  net_utilities.cpp                                                    */
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

/**
	@author AndreaCatania
*/

#include "net_utilities.h"
#include "core/object_data.h"

// This was needed to optimize the godot stringify for byte arrays.. it was slowing down perfs.
String NS::stringify_byte_array_fast(const Vector<uint8_t> &p_array) {
	CharString str;
	str.resize(p_array.size());
	memcpy(str.ptrw(), p_array.ptr(), p_array.size());
	return String(str);
}

String NS::stringify_fast(const Variant &p_var) {
	return p_var.get_type() == Variant::PACKED_BYTE_ARRAY ? stringify_byte_array_fast(p_var) : p_var.stringify();
}

bool NS::SyncGroup::is_realtime_node_list_changed() const {
	return realtime_sync_nodes_list_changed;
}

bool NS::SyncGroup::is_deferred_node_list_changed() const {
	return deferred_sync_nodes_list_changed;
}

const LocalVector<NS::SyncGroup::RealtimeNodeInfo> &NS::SyncGroup::get_realtime_sync_nodes() const {
	return realtime_sync_nodes;
}

const LocalVector<NS::SyncGroup::DeferredNodeInfo> &NS::SyncGroup::get_deferred_sync_nodes() const {
	return deferred_sync_nodes;
}

LocalVector<NS::SyncGroup::DeferredNodeInfo> &NS::SyncGroup::get_deferred_sync_nodes() {
	return deferred_sync_nodes;
}

void NS::SyncGroup::mark_changes_as_notified() {
	for (int i = 0; i < int(realtime_sync_nodes.size()); ++i) {
		realtime_sync_nodes[i].change.unknown = false;
		realtime_sync_nodes[i].change.uknown_vars.clear();
		realtime_sync_nodes[i].change.vars.clear();
	}
	for (int i = 0; i < int(deferred_sync_nodes.size()); ++i) {
		deferred_sync_nodes[i]._unknown = false;
	}
	realtime_sync_nodes_list_changed = false;
	deferred_sync_nodes_list_changed = false;
}

uint32_t NS::SyncGroup::add_new_node(ObjectData *p_object_data, bool p_realtime) {
	if (p_realtime) {
		// Make sure the node is not contained into the deferred sync.
		const int dsn_index = deferred_sync_nodes.find(p_object_data);
		if (dsn_index >= 0) {
			deferred_sync_nodes.remove_at_unordered(dsn_index);
			deferred_sync_nodes_list_changed = true;
		}

		// Add it into the realtime sync nodes
		int index = realtime_sync_nodes.find(p_object_data);

		if (index <= -1) {
			index = realtime_sync_nodes.size();
			realtime_sync_nodes.push_back(p_object_data);
			realtime_sync_nodes_list_changed = true;

			RealtimeNodeInfo &info = realtime_sync_nodes[index];

			info.change.unknown = true;

			for (int i = 0; i < int(p_object_data->vars.size()); ++i) {
				notify_new_variable(p_object_data, p_object_data->vars[i].var.name);
			}
		}

		return index;
	} else {
		// Make sure the node is not contained into the realtime sync.
		const int rsn_index = realtime_sync_nodes.find(p_object_data);
		if (rsn_index >= 0) {
			realtime_sync_nodes.remove_at_unordered(rsn_index);
			realtime_sync_nodes_list_changed = true;
		}

		// Add it into the deferred sync nodes
		int index = deferred_sync_nodes.find(p_object_data);

		if (index <= -1) {
			index = deferred_sync_nodes.size();
			deferred_sync_nodes.push_back(p_object_data);
			deferred_sync_nodes[index]._unknown = true;
			deferred_sync_nodes_list_changed = true;
		}

		return index;
	}
}

void NS::SyncGroup::remove_node(ObjectData *p_object_data) {
	{
		const int index = realtime_sync_nodes.find(p_object_data);
		if (index >= 0) {
			realtime_sync_nodes.remove_at_unordered(index);
			realtime_sync_nodes_list_changed = true;
			// No need to check the deferred array. Nodes can be in 1 single array.
			return;
		}
	}

	{
		const int index = deferred_sync_nodes.find(p_object_data);
		if (index >= 0) {
			deferred_sync_nodes.erase(p_object_data);
			deferred_sync_nodes_list_changed = true;
		}
	}
}

template <class T>
void replace_nodes_impl(
		NS::SyncGroup &p_sync_group,
		LocalVector<T> &&p_nodes_to_add,
		bool p_is_realtime,
		LocalVector<T> &r_sync_group_nodes,
		bool &r_changed) {
	for (int i = int(r_sync_group_nodes.size()) - 1; i >= 0; i--) {
		const int64_t nta_index = p_nodes_to_add.find(r_sync_group_nodes[i].od);
		if (nta_index == -1) {
			// This node is not part of this sync group, remove it.
			r_sync_group_nodes.remove_at_unordered(i);
			r_changed = true;
		} else {
			// This node is still part of this SyncGroup.
			// Update the existing one.
			r_sync_group_nodes[i].update_from(p_nodes_to_add[nta_index]);

			// Then, make sure not to add again:
			p_nodes_to_add.remove_at_unordered(nta_index);

#ifdef DEBUG_ENABLED
			// Make sure there are no duplicates:
			CRASH_COND_MSG(p_nodes_to_add.find(r_sync_group_nodes[i].od) != -1, "The function `replace_nodes` must receive unique nodes on each array. Make sure not to add duplicates.");
#endif
		}
	}

	// Add the missing nodes now.
	for (int i = 0; i < int(p_nodes_to_add.size()); i++) {
		NS::ObjectData *od = p_nodes_to_add[i].od;

#ifdef DEBUG_ENABLED
		CRASH_COND_MSG(r_sync_group_nodes.find(od) != -1, "[FATAL] This is impossible to trigger, because the above loop cleaned this.");
#endif

		const uint32_t index = p_sync_group.add_new_node(od, p_is_realtime);
		r_sync_group_nodes[index].update_from(p_nodes_to_add[i]);
	}
}

void NS::SyncGroup::replace_nodes(LocalVector<RealtimeNodeInfo> &&p_new_realtime_nodes, LocalVector<DeferredNodeInfo> &&p_new_deferred_nodes) {
	replace_nodes_impl(
			*this,
			std::move(p_new_realtime_nodes),
			true,
			realtime_sync_nodes,
			realtime_sync_nodes_list_changed);

	replace_nodes_impl(
			*this,
			std::move(p_new_deferred_nodes),
			false,
			deferred_sync_nodes,
			deferred_sync_nodes_list_changed);
}

void NS::SyncGroup::remove_all_nodes() {
	if (!realtime_sync_nodes.is_empty()) {
		realtime_sync_nodes.clear();
		realtime_sync_nodes_list_changed = true;
	}

	if (!deferred_sync_nodes.is_empty()) {
		deferred_sync_nodes.clear();
		deferred_sync_nodes_list_changed = true;
	}
}

void NS::SyncGroup::notify_new_variable(ObjectData *p_object_data, const StringName &p_var_name) {
	int index = realtime_sync_nodes.find(p_object_data);
	if (index >= 0) {
		realtime_sync_nodes[index].change.vars.insert(p_var_name);
		realtime_sync_nodes[index].change.uknown_vars.insert(p_var_name);
	}
}

void NS::SyncGroup::notify_variable_changed(ObjectData *p_object_data, const StringName &p_var_name) {
	int index = realtime_sync_nodes.find(p_object_data);
	if (index >= 0) {
		realtime_sync_nodes[index].change.vars.insert(p_var_name);
	}
}

void NS::SyncGroup::set_deferred_update_rate(NS::ObjectData *p_object_data, real_t p_update_rate) {
	const int index = deferred_sync_nodes.find(p_object_data);
	ERR_FAIL_COND(index < 0);
	deferred_sync_nodes[index].update_rate = p_update_rate;
}

real_t NS::SyncGroup::get_deferred_update_rate(const NS::ObjectData *p_object_data) const {
	for (int i = 0; i < int(deferred_sync_nodes.size()); ++i) {
		if (deferred_sync_nodes[i].od == p_object_data) {
			return deferred_sync_nodes[i].update_rate;
		}
	}
	ERR_PRINT(String() + "NodeData " + p_object_data->object_name.c_str() + " not found into `deferred_sync_nodes`.");
	return 0.0;
}

void NS::SyncGroup::sort_deferred_node_by_update_priority() {
	struct DNIComparator {
		_FORCE_INLINE_ bool operator()(const DeferredNodeInfo &a, const DeferredNodeInfo &b) const {
			return a._update_priority > b._update_priority;
		}
	};

	deferred_sync_nodes.sort_custom<DNIComparator>();
}
