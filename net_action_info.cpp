#include "net_action_info.h"

bool NetActionInfo::operator==(const NetActionInfo &p_other) const {
	return act_func == p_other.act_func;
}

bool NetActionInfo::operator<(const NetActionInfo &p_other) const {
	return id < p_other.id;
}