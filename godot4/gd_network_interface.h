#pragma once

#include "core/object/object.h"
#include "modules/network_synchronizer/core/network_interface.h"

class GdNetworkInterface : public NS::NetworkInterface,
						   public Object {
public:
	class Node *owner = nullptr;
	std::function<void(int /*p_peer*/)> on_peer_connected_callback;
	std::function<void(int /*p_peer*/)> on_peer_disconnected_callback;

public:
	GdNetworkInterface();
	virtual ~GdNetworkInterface();

public: // ---------------------------------------------------------------- APIs
	virtual String get_name() const override;

	virtual int get_server_peer() const override { return 1; }

	/// Call this function to start receiving events on peer connection / disconnection.
	virtual void start_listening_peer_connection(
			std::function<void(int /*p_peer*/)> p_on_peer_connected_callback,
			std::function<void(int /*p_peer*/)> p_on_peer_disconnected_callback) override;

	/// Emitted when a new peers connects.
	void on_peer_connected(int p_peer);
	static void __on_peer_connected(GdNetworkInterface *p_ni, int p_peer);

	/// Emitted when a peers disconnects.
	void on_peer_disconnected(int p_peer);

	/// Call this function to stop receiving events on peer connection / disconnection.
	virtual void stop_listening_peer_connection() override;

	/// Fetch the current client peer_id
	virtual int fetch_local_peer_id() const override;

	/// Fetch the list with all the connected peers.
	virtual void fetch_connected_peers(std::vector<int> &p_connected_peers) const override;

	/// Get the peer id controlling this unit.
	virtual int get_unit_authority() const override;

	/// Can be used to verify if the local peer is connected to a server.
	virtual bool is_local_peer_networked() const override;

	/// Can be used to verify if the local peer is the server.
	virtual bool is_local_peer_server() const override;

	/// Can be used to verify if the local peer is the authority of this unit.
	virtual bool is_local_peer_authority_of_this_unit() const override;

	virtual void encode(DataBuffer &r_buffer, const NS::VarData &p_val) const override;
	virtual void decode(NS::VarData &r_val, DataBuffer &p_buffer) const override;

	static void convert(Variant &r_variant, const NS::VarData &p_vd);
	static void convert(NS::VarData &r_vd, const Variant &p_variant);

	virtual bool compare(const NS::VarData &p_A, const NS::VarData &p_B) const override;
	virtual bool compare(const Variant &p_first, const Variant &p_second) const override;

	static bool compare_static(const NS::VarData &p_A, const NS::VarData &p_B);
	static bool compare(const Vector2 &p_first, const Vector2 &p_second, real_t p_tolerance);
	/// Returns true when the vectors are the same.
	static bool compare(const Vector3 &p_first, const Vector3 &p_second, real_t p_tolerance);
	/// Returns true when the variants are the same.
	static bool compare(const Variant &p_first, const Variant &p_second, real_t p_tolerance);

	virtual void rpc_send(int p_peer_recipient, bool p_reliable, DataBuffer &&p_buffer) override;
	void gd_rpc_receive(const Vector<uint8_t> &p_args);
};

namespace NS_GD_Test {
void test_var_data_conversin();
};
