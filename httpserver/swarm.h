#pragma once

#include <iostream>
#include <string>
#include <vector>

#include "arqma_common.h"

namespace boost
{
namespace asio
{
class io_context;
} // namespace asio
} // namespace boost

namespace arqma {

class ServiceNode;

struct SwarmInfo {
    swarm_id_t swarm_id;
    std::vector<sn_record_t> snodes;
};

using all_swarms_t = std::vector<SwarmInfo>;

struct block_update_t {
    all_swarms_t swarms;
    std::vector<sn_record_t> decommissioned_nodes;
    uint64_t height;
    std::string block_hash;
    int hardfork;
    bool unchanged = false;
};

swarm_id_t get_swarm_by_pk(const std::vector<SwarmInfo>& all_swarms,
                           const user_pubkey_t& pk);

auto apply_ips(const all_swarms_t& swarms_to_keep, const all_swarms_t& other_swarms) -> all_swarms_t;

struct SwarmEvents {

    /// our (potentially new) swarm id
    swarm_id_t our_swarm_id;
    /// whether our swarm got dissolved and we
    /// need to salvage our stale data
    bool dissolved = false;
    /// detected new swarms that need to be bootstrapped
    std::vector<swarm_id_t> new_swarms;
    /// detected new snodes in our swarm
    std::vector<sn_record_t> new_snodes;
    /// our swarm members 
    std::vector<sn_record_t> our_swarm_members;
};

class Swarm {

    swarm_id_t cur_swarm_id_ = INVALID_SWARM_ID;
    std::vector<SwarmInfo> all_valid_swarms_;
    sn_record_t our_address_;
    std::vector<sn_record_t> swarm_peers_;

    std::vector<sn_record_t> all_funded_nodes_;

    bool is_existing_swarm(swarm_id_t sid) const;

  public:
    Swarm(sn_record_t address) : our_address_(address) {}

    ~Swarm();

    /// Extract relevant information from incoming swarm composition
    SwarmEvents derive_swarm_events(const all_swarms_t& swarms) const;

    /// Update swarm state according to `events`
    void update_state(const all_swarms_t& swarms,
                      const std::vector<sn_record_t>& decommissioned,
                      const SwarmEvents& events);

    void apply_swarm_changes(const all_swarms_t& new_swarms);

    bool is_pubkey_for_us(const user_pubkey_t& pk) const;

    bool is_fully_funded_node(const std::string& sn_address) const;

    const std::vector<sn_record_t>& other_nodes() const;

    const std::vector<SwarmInfo>& all_valid_swarms() const {
        return all_valid_swarms_;
    }

    swarm_id_t our_swarm_id() const { return cur_swarm_id_; }

    bool is_valid() const { return cur_swarm_id_ != INVALID_SWARM_ID; }

    void set_swarm_id(swarm_id_t sid);

    boost::optional<sn_record_t> choose_funded_node() const;
    boost::optional<sn_record_t> find_node_by_port(uint16_t port) const;
    boost::optional<sn_record_t> get_node_by_pk(const sn_pub_key_t& pk) const;
    boost::optional<sn_record_t> find_node_by_ed25519_pk(const sn_pub_key_t& address) const;
};

} // namespace arqma
