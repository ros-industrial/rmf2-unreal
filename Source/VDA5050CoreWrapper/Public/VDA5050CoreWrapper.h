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

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

struct FVDA5050Node
{
  std::string NodeId;
  uint32_t SequenceId;
  double X, Y;
  std::optional<double> Theta;
};

class VDA5050COREWRAPPER_API FVDA5050Client
{
public:
  FVDA5050Client();

  ~FVDA5050Client();

  bool Connect(
      const std::string& BrokerAddress,
      const std::string& InterfaceName,
      const std::string& Version,
      const std::string& Manufacturer,
      const std::string& SerialNumber
  );

  void Disconnect();

  void ClientNodeAck(uint32_t SequenceId);

  void ReportPose(double X, double Y, double Theta);

  std::function<void(const FVDA5050Node&)> OnNodeDispatch;

private:
  struct FImpl;
  std::unique_ptr<FImpl> Impl;
};
