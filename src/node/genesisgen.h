// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once
#include "codeid.h"
#include "entities.h"
#include "luainterp/luainterp.h"
#include "luainterp/luautil.h"
#include "members.h"
#include "networktables.h"
#include "nodeinfonetwork.h"
#include "nodes.h"
#include "rpc/consts.h"
#include "runtime_config/default_whitelists.h"
#include "tls/verifier.h"
#include "values.h"

#include <algorithm>
#include <fstream>
#include <ostream>

namespace ccf
{
  class GenesisGenerator
  {
    NetworkTables& tables;

    Store::Tx& tx;

    template <typename T>
    void set_scripts(
      std::map<std::string, std::string> scripts,
      T& table,
      const bool compile = false)
    {
      auto tx_scripts = tx.get_view(table);
      for (auto& rs : scripts)
      {
        if (compile)
          tx_scripts->put(rs.first, lua::compile(rs.second));
        else
          tx_scripts->put(rs.first, rs.second);
      }
    }

  public:
    GenesisGenerator(NetworkTables& tables_, Store::Tx& tx_) :
      tables(tables_),
      tx(tx_)
    {}

    void init_values()
    {
      auto v = tx.get_view(tables.values);
      for (int id_type = 0; id_type < ValueIds::END_ID; id_type++)
        v->put(id_type, 0);
    }

    auto finalize()
    {
      return tx.commit();
    }

    void retire_active_nodes()
    {
      auto nodes_view = tx.get_view(tables.nodes);

      std::map<NodeId, NodeInfo> nodes_to_delete;
      nodes_view->foreach(
        [&nodes_to_delete](const NodeId& nid, const NodeInfo& ni) {
          // Only retire nodes that have not already been retired
          if (ni.status != NodeStatus::RETIRED)
            nodes_to_delete[nid] = ni;
          return true;
        });

      for (auto [nid, ni] : nodes_to_delete)
      {
        ni.status = NodeStatus::RETIRED;
        nodes_view->put(nid, ni);
      }
    }

    auto add_member(
      const std::vector<uint8_t>& member_cert_pem,
      const std::vector<uint8_t>& member_keyshare_pub,
      MemberStatus member_status = MemberStatus::ACTIVE)
    {
      auto [m, mc, v] =
        tx.get_view(tables.members, tables.member_certs, tables.values);

      // Input certificates are generated by members and will not be
      // null-terminated
      auto pem = tls::Pem(member_cert_pem);
      auto member_cert_der =
        tls::make_verifier({pem.data(), pem.data() + pem.size()})
          ->der_cert_data();
      auto member_id = mc->get(member_cert_der);

      // Cert should be unique
      if (member_id.has_value())
      {
        throw std::logic_error(fmt::format(
          "Member certificate already exists (member {})", member_id.value()));
      }

      const auto id = get_next_id(v, ValueIds::NEXT_MEMBER_ID);
      m->put(
        id, MemberInfo(member_cert_der, member_keyshare_pub, member_status));
      mc->put(member_cert_der, id);
      return id;
    }

    auto add_user(const std::vector<uint8_t>& user_cert_pem)
    {
      auto [u, uc, v] =
        tx.get_view(tables.users, tables.user_certs, tables.values);

      // Input certificates are generated by users and will not be
      // null-terminated
      auto pem = tls::Pem(user_cert_pem);
      auto user_cert_der =
        tls::make_verifier({pem.data(), pem.data() + pem.size()})
          ->der_cert_data();

      // Cert should be unique
      auto user_id = uc->get(user_cert_der);
      if (user_id.has_value())
      {
        throw std::logic_error(fmt::format(
          "User certificate already exists (user {})", user_id.value()));
      }

      const auto id = get_next_id(v, ValueIds::NEXT_USER_ID);
      u->put(id, {user_cert_der});
      uc->put(user_cert_der, id);
      return id;
    }

    auto add_node(const NodeInfo& node_info)
    {
      auto node_id =
        get_next_id(tx.get_view(tables.values), ValueIds::NEXT_NODE_ID);

      auto raw_cert = tls::make_verifier(node_info.cert)->der_cert_data();

      auto node_view = tx.get_view(tables.nodes);
      node_view->put(node_id, node_info);
      return node_id;
    }

    auto get_trusted_nodes(std::optional<NodeId> self_to_exclude = std::nullopt)
    {
      // Returns the list of trusted nodes. If self_to_exclude is set,
      // self_to_exclude is not included in the list of returned nodes.
      std::map<NodeId, NodeInfo> active_nodes;

      auto [nodes_view, secrets_view] =
        tx.get_view(tables.nodes, tables.secrets);

      nodes_view->foreach([&active_nodes, self_to_exclude, this](
                            const NodeId& nid, const NodeInfo& ni) {
        if (
          ni.status == ccf::NodeStatus::TRUSTED &&
          (!self_to_exclude.has_value() || self_to_exclude.value() != nid))
        {
          active_nodes[nid] = ni;
        }
        return true;
      });

      return active_nodes;
    }

    void create_service(
      const std::vector<uint8_t>& network_cert, kv::Version version = 1)
    {
      auto service_view = tx.get_view(tables.service);
      service_view->put(0, {version, network_cert, ServiceStatus::OPENING});
    }

    bool is_service_created()
    {
      auto service_view = tx.get_view(tables.service);
      return service_view->get(0).has_value();
    }

    bool open_service()
    {
      auto service_view = tx.get_view(tables.service);

      auto active_service = service_view->get(0);
      if (!active_service.has_value())
      {
        LOG_FAIL_FMT("Failed to get active service");
        return false;
      }

      if (active_service->status != ServiceStatus::OPENING)
      {
        LOG_FAIL_FMT("Could not open current service: status is not OPENING");
        return false;
      }

      active_service->status = ServiceStatus::OPEN;
      service_view->put(0, active_service.value());

      return true;
    }

    void trust_node(NodeId node_id)
    {
      auto nodes_view = tx.get_view(tables.nodes);
      auto node_info = nodes_view->get(node_id);
      if (node_info.has_value())
      {
        node_info->status = NodeStatus::TRUSTED;
        nodes_view->put(node_id, node_info.value());
      }
      else
      {
        LOG_FAIL_FMT("Unknown node {} could not be trusted");
      }
    }

    auto get_last_signature()
    {
      auto sig_view = tx.get_view(tables.signatures);
      return sig_view->get(0);
    }

    void set_whitelist(WlIds id, Whitelist wl)
    {
      tx.get_view(tables.whitelists)->put(id, wl);
    }

    void set_gov_scripts(std::map<std::string, std::string> scripts)
    {
      // don't compile, because gov scripts are important functionally but not
      // performance-wise
      set_scripts(scripts, tables.gov_scripts, false);
    }

    void set_app_scripts(std::map<std::string, std::string> scripts)
    {
      set_scripts(scripts, tables.app_scripts, true);
    }

    void trust_code_id(CodeDigest& node_code_id)
    {
      auto codeid_view = tx.get_view(tables.code_ids);
      codeid_view->put(node_code_id, CodeStatus::ACCEPTED);
    }

    auto get_active_members_keyshare()
    {
      auto members_view = tx.get_view(tables.members);
      std::map<MemberId, std::vector<uint8_t>> active_members_info;

      members_view->foreach(
        [&active_members_info](const MemberId& mid, const MemberInfo& mi) {
          if (mi.status == MemberStatus::ACTIVE)
          {
            active_members_info[mid] = mi.keyshare;
          }
          return true;
        });
      return active_members_info;
    }

    void add_key_share_info(const KeyShareInfo& key_share_info)
    {
      auto [shares_view, values_view] =
        tx.get_view(tables.shares, tables.values);
      auto keyshare_id = get_next_id(values_view, ValueIds::NEXT_KEYSHARE_ID);

      shares_view->put(keyshare_id, key_share_info);
    }
  };
}