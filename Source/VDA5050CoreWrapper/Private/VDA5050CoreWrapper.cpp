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
#include <string>

#include "vda5050_core/execution/protocol_adapter.hpp"
#include "vda5050_core/logger/logger.hpp"
#include "vda5050_core/transport/mqtt_client_interface.hpp"

#include "vda5050_core/client/adapter/action_execution.hpp"
#include "vda5050_core/client/adapter/action_request.hpp"
#include "vda5050_core/client/adapter/adapter.hpp"
#include "vda5050_core/client/adapter/edge_request.hpp"
#include "vda5050_core/client/adapter/localization_request.hpp"
#include "vda5050_core/client/adapter/node_request.hpp"
#include "vda5050_core/client/adapter/order_execution.hpp"
#include "vda5050_core/client/adapter/state_manager.hpp"

using vda5050_core::client::adapter::ActionExecution;
using vda5050_core::client::adapter::ActionRequest;
using vda5050_core::client::adapter::Adapter;
using vda5050_core::client::adapter::EdgeRequest;
using vda5050_core::client::adapter::LocalizationRequest;
using vda5050_core::client::adapter::NodeRequest;
using vda5050_core::client::adapter::OrderExecution;
using vda5050_core::client::adapter::StateManager;
using vda5050_core::execution::ProtocolAdapter;

struct FVDA5050Client::FImpl
{
  std::shared_ptr<ProtocolAdapter> protocol_adapter;
  std::shared_ptr<Adapter> adapter;
  std::shared_ptr<StateManager> state_manager;
  std::shared_ptr<OrderExecution> active_navigation;
  std::mutex mutex;
  std::string map_id;
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
  auto mqtt_client = vda5050_core::transport::create_default_client_unique(
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
  Impl->adapter = Adapter::make(Impl->protocol_adapter);

  Impl->state_manager = Impl->adapter->state_manager();

  Impl->adapter->on_navigate(
      [this](
          NodeRequest node_request,
          std::optional<EdgeRequest> edge_request,
          std::shared_ptr<OrderExecution> execution
      )
      {
        const auto& position = node_request.node_position();
        if (!position.has_value())
        {
          execution->failed("Requested node does not contain a position");
          return;
        }
        {
          std::lock_guard<std::mutex> lock(Impl->mutex);
          Impl->active_navigation = std::move(execution);
          Impl->map_id = position->map_id;
        }
        Impl->state_manager->set_driving(true);

        // On active navigation, we dispatch node_request to the UE5 delegate
        if (OnNodeDispatch)
        {
          FVDA5050Node node;
          node.NodeId = node_request.node_id();
          node.SequenceId = node_request.sequence_id();
          node.X = position->x;
          node.Y = position->y;
          node.Theta = position->theta;
          OnNodeDispatch(node);
        }
      }
  );

  Impl->adapter->on_action(
      [this](ActionRequest request, std::shared_ptr<ActionExecution> execution)
      { execution->finished(); }
  );

  Impl->adapter->on_localize(
      [this](
          LocalizationRequest request,
          std::shared_ptr<ActionExecution> execution
      )
      {
        {
          std::lock_guard<std::mutex> lock(Impl->mutex);
          Impl->map_id = request.map_id();
        }
        Impl->state_manager->initialize_position(
            request.x(),
            request.y(),
            request.theta(),
            request.map_id()
        );

        execution->finished();
      }
  );
  try
  {
    Impl->adapter->start();
  }
  catch (const std::exception& e)
  {
    VDA5050_ERROR(
        "Failed to start adapter: {}-UE5_VDA5050Client, {}",
        SerialNumber,
        e.what()
    );
    Impl->adapter.reset();
    Impl->state_manager.reset();
    Impl->protocol_adapter.reset();
    return false;
  }
  return true;
}

void FVDA5050Client::ClientNodeAck(uint32_t SequenceId)
{
  std::shared_ptr<OrderExecution> execution;
  {
    std::lock_guard<std::mutex> lock(Impl->mutex);
    execution = std::move(Impl->active_navigation);
  }
  if (execution)
  {
    Impl->state_manager->set_driving(false);
    execution->finished();
  }
}

void FVDA5050Client::ReportPose(double X, double Y, double Theta)
{
  if (!Impl || !Impl->state_manager)
  {
    return;
  }
  std::string map_id;
  {
    std::lock_guard<std::mutex> lock(Impl->mutex);
    map_id = Impl->map_id;
  }
  Impl->state_manager->set_position(X, Y, Theta, map_id);
}

void FVDA5050Client::Disconnect()
{
  if (!Impl || !Impl->adapter)
  {
    return;
  }
  Impl->adapter->stop();
  Impl->adapter.reset();
  Impl->state_manager.reset();
  Impl->active_navigation.reset();
}
