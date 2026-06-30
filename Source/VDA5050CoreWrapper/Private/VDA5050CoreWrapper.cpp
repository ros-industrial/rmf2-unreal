/*
 * Copyright (C) 2025-2026 ROS-Industrial Consortium Asia Pacific
 * Advanced Remanufacturing and Technology Centre
 * A*STAR Research Entities (Co. Registration No. 199702110H)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "VDA5050CoreWrapper.h"

#include <memory>
#include <mutex>
#include <typeindex>
#include <unordered_map>

#include <vda5050_core/logger/logger.hpp>
#include <vda5050_core/mqtt_client/mqtt_client_interface.hpp>

#include <vda5050_types/connection.hpp>
#include <vda5050_types/order.hpp>
#include <vda5050_types/state.hpp>

#include "vda5050_execution/base.hpp"
#include "vda5050_execution/context_interface.hpp"
#include "vda5050_execution/handler.hpp"
#include "vda5050_execution/protocol_adapter.hpp"
#include "vda5050_execution/strategy_interface.hpp"

using vda5050_execution::ContextInterface;
using vda5050_execution::EventBase;
using vda5050_execution::Handler;
using vda5050_execution::Initialize;
using vda5050_execution::Priority;
using vda5050_execution::ProtocolAdapter;
using vda5050_execution::ResourceBase;
using vda5050_execution::StrategyInterface;
using vda5050_execution::UpdateBase;

struct OrderUpdate : public Initialize<OrderUpdate, UpdateBase>
{
  vda5050_types::Order order;

  explicit OrderUpdate(const vda5050_types::Order& order)
      : order(std::move(order))
  {
    // Nothing to do here ...
  }
};

struct NodeDispatchEvent : public Initialize<NodeDispatchEvent, EventBase>
{
  uint32_t sequence_id;
  double x, y;
  std::optional<double> theta;

  NodeDispatchEvent(
      uint32_t sequence_id,
      double x,
      double y,
      std::optional<double> theta = std::nullopt
  )
      : sequence_id(sequence_id), x(x), y(y), theta(theta)
  {
  }
};

struct NodeAckUpdate : public Initialize<NodeAckUpdate, UpdateBase>
{
  uint32_t sequence_id;

  explicit NodeAckUpdate(uint32_t sequence_id) : sequence_id(sequence_id)
  {
    // Nothing to do here ...
  }
};

class NavigationStrategy : public StrategyInterface
{
public:
  std::function<void(const FVDA5050Node&)>* on_node_dispatch = nullptr;

  void init(std::shared_ptr<ContextInterface> context) override
  {
    VDA5050_INFO("Initializing NavigationStrategy ...");

    engine()->on<NodeDispatchEvent>(
        [this](auto event)
        {
          if (on_node_dispatch && *on_node_dispatch)
          {
            FVDA5050Node node;
            node.SequenceId = event->sequence_id;
            node.X = event->x;
            node.Y = event->y;
            node.Theta = event->theta;
            (*on_node_dispatch)(node);
          }
        }
    );

    context->provider()->on<NodeAckUpdate>(
        [w = std::weak_ptr(engine())](auto update)
        {
          if (auto m = w.lock())
            m->notify(update);
        }
    );
  }

  void step(std::shared_ptr<ContextInterface> context) override
  {
    if (engine()->waiting())
      return;

    if (nodes_.empty())
    {
      auto order_update = context->get_update<OrderUpdate>();
      if (!order_update)
      {
        return;
      }
      bool is_new_order = (order_update->order.order_id != current_order_id_);
      if (is_new_order ||
          order_update->order.order_update_id > current_order_update_id_)
      {
        VDA5050_INFO(
            "{} - orderId: {}, updateId: {}",
            is_new_order ? "New order" : "Order update",
            order_update->order.order_id,
            order_update->order.order_update_id
        );
        nodes_ = order_update->order.nodes;
        current_order_id_ = order_update->order.order_id;
        current_order_update_id_ = order_update->order.order_update_id;
        current_idx_ = 0;
        order_completed_ = false;
      }
      else
      {
        return;
      }
    }

    if (current_idx_ < nodes_.size())
    {
      auto target = nodes_[current_idx_++];
      engine()->emit<NodeDispatchEvent>(
          Priority::NORMAL,
          target.sequence_id,
          target.node_position.value().x,
          target.node_position.value().y,
          target.node_position.value().theta.value_or(0)
      );

      VDA5050_INFO(
          "Pushing node with sequence_id [{}] to event queue",
          target.sequence_id
      );

      engine()->step();

      engine()->suspend<NodeAckUpdate>(
          [seq = target.sequence_id](auto update) -> bool
          { return update->sequence_id == seq; }
      );
    }
    else
    {
      order_completed_ = true;
      nodes_.clear();
    }
  }

private:
  std::vector<vda5050_types::Node> nodes_;
  size_t current_idx_ = 0;
  uint32_t current_order_update_id_ = 0;
  std::string current_order_id_;
  bool order_completed_ = false;
};

class SimpleContext : public ContextInterface,
                      public std::enable_shared_from_this<SimpleContext>
{
public:
  void init() override
  {
    provider()->on<OrderUpdate>(
        [w = weak_from_this()](auto update)
        {
          if (auto m = w.lock())
          {
            std::lock_guard<std::mutex> lock(m->mutex_);
            m->updates_[update->get_type()] = update;
          }
        }
    );
  }

protected:
  std::shared_ptr<UpdateBase> get_update_raw(std::type_index type
  ) const override
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return (updates_.count(type)) ? updates_.at(type) : nullptr;
  }

  std::shared_ptr<ResourceBase>
  get_resource_raw(std::type_index /*type*/) const override
  {
    return nullptr;
  }

private:
  std::unordered_map<std::type_index, std::shared_ptr<UpdateBase>> updates_;
  mutable std::mutex mutex_;
};

struct AGVState
{
  mutable std::mutex mutex;
  std::string order_id;
  uint32_t order_update_id = 0;
  std::string last_node_id;
  uint32_t last_node_sequence_id = 0;
  std::vector<vda5050_types::NodeState> node_states;
  std::vector<vda5050_types::EdgeState> edge_states;
  double x = 0.0, y = 0.0, theta = 0.0;
  std::string map_id;
  bool driving = false;
  bool publish_enabled = false;
  bool event_triggered = false; // Triggers publish call immediately.
};

class StateStrategy : public StrategyInterface
{
public:
  std::function<void(double& X, double& Y, double& Theta)>*
      on_position_request = nullptr;

  explicit StateStrategy(
      std::shared_ptr<ProtocolAdapter> protocol_adapter,
      std::shared_ptr<AGVState> agv_state
  )
      : protocol_adapter_(protocol_adapter),
        agv_state_(agv_state),
        last_pub_time_(std::chrono::steady_clock::now())
  {
  }

  void init(std::shared_ptr<ContextInterface> /*context*/) override {}

  void step(std::shared_ptr<ContextInterface> /*context*/) override
  {
    if (!agv_state_->publish_enabled)
      return;

    bool publish_now;
    {
      std::lock_guard<std::mutex> lock(agv_state_->mutex);
      publish_now = agv_state_->event_triggered;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed =
        std::chrono::duration_cast<std::chrono::seconds>(now - last_pub_time_)
            .count();

    if (!publish_now && elapsed < 30)
      return;

    last_pub_time_ = now;
    publish_state();
  }

private:
  std::shared_ptr<ProtocolAdapter> protocol_adapter_;
  std::shared_ptr<AGVState> agv_state_;
  std::chrono::steady_clock::time_point last_pub_time_;

  void publish_state()
  {
    if (on_position_request && *on_position_request)
    {
      double x, y, theta;
      (*on_position_request)(x, y, theta);
      std::lock_guard<std::mutex> pos_lock(agv_state_->mutex);
      agv_state_->x = x;
      agv_state_->y = y;
      agv_state_->theta = theta;
    }

    std::lock_guard<std::mutex> lock(agv_state_->mutex);
    agv_state_->event_triggered = false;

    vda5050_types::State state;
    state.order_id = agv_state_->order_id;
    state.order_update_id = agv_state_->order_update_id;
    state.last_node_id = agv_state_->last_node_id;
    state.last_node_sequence_id = agv_state_->last_node_sequence_id;
    state.node_states = agv_state_->node_states;
    state.edge_states = agv_state_->edge_states;
    state.driving = agv_state_->driving;
    state.operating_mode = vda5050_types::OperatingMode::AUTOMATIC;
    state.battery_state.battery_charge = 100.0;
    state.battery_state.charging = false;
    state.safety_state.e_stop = vda5050_types::EStop::NONE;
    state.safety_state.field_violation = false;

    vda5050_types::AGVPosition pos;
    pos.x = agv_state_->x;
    pos.y = agv_state_->y;
    pos.theta = agv_state_->theta;
    pos.map_id = agv_state_->map_id;
    state.agv_position = pos;

    protocol_adapter_->publish<vda5050_types::State>(state, 0, false);
  }
};

struct FVDA5050Client::FImpl
{
  std::shared_ptr<ProtocolAdapter> protocol_adapter;
  std::shared_ptr<SimpleContext> context;
  std::shared_ptr<NavigationStrategy> navigation_strategy;
  std::shared_ptr<StateStrategy> state_strategy;
  std::shared_ptr<Handler> handler;
  std::shared_ptr<AGVState> agv_state;
};

FVDA5050Client::FVDA5050Client() : Impl(std::make_unique<FImpl>()) {}

FVDA5050Client::~FVDA5050Client() { Disconnect(); }

bool FVDA5050Client::Connect(
    const std::string& BrokerAddress,
    const std::string& InterfaceName,
    const std::string& Version,
    const std::string& Manufacturer,
    const std::string& SerialNumber
)
{
  auto mqtt_client = vda5050_core::mqtt_client::create_default_client_unique(
      BrokerAddress,
      SerialNumber + "-UE5_VDA5050Client"
  );
  Impl->protocol_adapter = ProtocolAdapter::make(
      std::move(mqtt_client),
      InterfaceName,
      Version,
      Manufacturer,
      SerialNumber
  );
  vda5050_types::Connection connection_will;
  connection_will.connection_state =
      vda5050_types::ConnectionState::CONNECTIONBROKEN;
  Impl->protocol_adapter
      ->set_will<vda5050_types::Connection>(connection_will, 1, true);

  try
  {
    Impl->protocol_adapter->connect();
  }
  catch (const std::exception& e)
  {
    VDA5050_ERROR("Failed to connect to broker: {}", e.what());
    Impl->protocol_adapter.reset();
    return false;
  }

  Impl->agv_state = std::make_shared<AGVState>();
  Impl->context = std::make_shared<SimpleContext>();
  Impl->navigation_strategy = std::make_shared<NavigationStrategy>();
  Impl->state_strategy =
      std::make_shared<StateStrategy>(Impl->protocol_adapter, Impl->agv_state);
  Impl->state_strategy->on_position_request = &OnPositionRequest;
  Impl->handler = Handler::make(
      Impl->context,
      {Impl->navigation_strategy, Impl->state_strategy}
  );
  Impl->navigation_strategy->on_node_dispatch = &OnNodeDispatch;

  Impl->protocol_adapter->subscribe<vda5050_types::Order>(
      [this,
       w = std::weak_ptr<ContextInterface>(Impl->context),
       agv_state = Impl->agv_state](auto order, auto error)
      {
        if (error.has_value())
          return;

        if (auto m = w.lock())
        {
          m->provider()->push<OrderUpdate>(order);
        }

        // Update shared AGV state with the new order info
        {
          std::lock_guard<std::mutex> lock(agv_state->mutex);
          agv_state->order_id = order.order_id;
          agv_state->order_update_id = order.order_update_id;
          agv_state->node_states.clear();
          agv_state->edge_states.clear();
          for (const auto& node : order.nodes)
          {
            vda5050_types::NodeState node_state;
            node_state.node_id = node.node_id;
            node_state.sequence_id = node.sequence_id;
            node_state.released = node.released;
            if (node.node_position.has_value())
            {
              vda5050_types::NodePosition node_pos;
              node_pos.x = node.node_position->x;
              node_pos.y = node.node_position->y;
              node_pos.map_id = node.node_position->map_id;
              node_state.node_position = node_pos;
            }
            agv_state->node_states.push_back(node_state);
          }
          agv_state->event_triggered = true;
        }

        if (OnOrderReceived)
        {
          FVDA5050Order forder;
          forder.OrderId = order.order_id;
          forder.OrderUpdateId = order.order_update_id;
          for (const auto& node : order.nodes)
          {
            FVDA5050Node fnode;
            fnode.NodeId = node.node_id;
            fnode.SequenceId = node.sequence_id;
            if (node.node_position.has_value())
            {
              fnode.X = node.node_position->x;
              fnode.Y = node.node_position->y;
              fnode.Theta = node.node_position->theta;
              forder.Nodes.push_back(fnode);
            }
          }
          OnOrderReceived(forder);
        }
      },
      0
  );

  vda5050_types::Connection connection_online;
  connection_online.connection_state = vda5050_types::ConnectionState::ONLINE;
  Impl->protocol_adapter
      ->publish<vda5050_types::Connection>(connection_online, 1, true);

  return true;
}

void FVDA5050Client::ClientNodeAck(uint32_t SequenceId)
{
  if (Impl && Impl->context)
  {
    Impl->context->provider()->push<NodeAckUpdate>(SequenceId);
  }

  // Update shared AGV state: mark node as reached, remove from remaining
  if (Impl && Impl->agv_state)
  {
    std::lock_guard<std::mutex> lock(Impl->agv_state->mutex);
    auto& node_state = Impl->agv_state->node_states;
    for (auto it = node_state.begin(); it != node_state.end(); ++it)
    {
      if (it->sequence_id == SequenceId)
      {
        Impl->agv_state->last_node_id = it->node_id;
        Impl->agv_state->last_node_sequence_id = it->sequence_id;
        node_state.erase(it);
        break;
      }
    }
    Impl->agv_state->driving = !node_state.empty();
    Impl->agv_state->event_triggered = true;
  }
}

void FVDA5050Client::SetPublishState(bool bEnabled)
{
  if (Impl && Impl->agv_state)
  {
    std::lock_guard<std::mutex> lock(Impl->agv_state->mutex);
    Impl->agv_state->publish_enabled = bEnabled;
  }
}

void FVDA5050Client::Disconnect()
{
  if (!Impl || !Impl->protocol_adapter)
  {
    return;
  }
  if (Impl->handler)
  {
    Impl->handler->stop();
  }
  vda5050_types::Connection connection_offline;
  connection_offline.connection_state = vda5050_types::ConnectionState::OFFLINE;
  Impl->protocol_adapter
      ->publish<vda5050_types::Connection>(connection_offline, 1, true);

  Impl->protocol_adapter->disconnect();
  Impl->protocol_adapter.reset();
}

void FVDA5050Client::SpinOnce()
{
  if (Impl && Impl->handler)
  {
    Impl->handler->spin_once();
  }
}
