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

#include "VDA5050ClientComponent.h"
#include "VDA5050CoreWrapper.h"

#include "Async/Async.h"

static constexpr double CM_TO_M = 100.0;

UVDA5050ClientComponent::UVDA5050ClientComponent()
{
  PrimaryComponentTick.bCanEverTick = true;
}

void UVDA5050ClientComponent::BeginPlay()
{
  Super::BeginPlay();

  Client = MakeShared<FVDA5050Client>();

  Client->OnNodeDispatch = [this](const FVDA5050Node& Node)
  {
    // Need to specify game thread to run task as although OnNodeDispatch is
    // called from the adapter thread, Broadcast() runs on game thread
    AsyncTask(
        ENamedThreads::GameThread,
        [this, Node]()
        {
          FVDA5050NodeInfo NodeInfo;
          NodeInfo.NodeId = UTF8_TO_TCHAR(Node.NodeId.c_str());
          NodeInfo.SequenceId = Node.SequenceId;
          NodeInfo.Position = FVector(
              Node.X * CM_TO_M,
              Node.Y * CM_TO_M,
              GetOwner()->GetActorLocation().Z
          );
          NodeInfo.Theta = Node.Theta.value_or(0);
          OnNodeDispatch.Broadcast(NodeInfo);
        }
    );
  };

  if (bAutoConnect)
  {
    Connect(BrokerAddress, InterfaceName, Version, Manufacturer, SerialNumber);
  }
}

void UVDA5050ClientComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
  Disconnect();
  Client.Reset();
  Super::EndPlay(EndPlayReason);
}

void UVDA5050ClientComponent::Connect(
    const FString& InBrokerAddress,
    const FString& InInterfaceName,
    const FString& InVersion,
    const FString& InManufacturer,
    const FString& InSerialNumber
)
{
  if (!Client)
    return;

  FString broker_address = InBrokerAddress;
  FString interface_name = InInterfaceName;
  FString version = InVersion;
  FString manufacturer = InManufacturer;
  FString serial_number = InSerialNumber;

  // Might be a better idea to specify specific threads to run this on. As of
  // now no apparent implicationss using AnyThread
  AsyncTask(
      ENamedThreads::AnyThread,
      [this,
       broker_address,
       interface_name,
       version,
       manufacturer,
       serial_number]()
      {
        // This code will run asynchronously, without freezing the game thread.
        bool bSuccess = Client->Connect(
            TCHAR_TO_UTF8(*broker_address),
            TCHAR_TO_UTF8(*interface_name),
            TCHAR_TO_UTF8(*version),
            TCHAR_TO_UTF8(*manufacturer),
            TCHAR_TO_UTF8(*serial_number)
        );
        // TODO(DillonChew98): Add case to handle connection failure.
        // while (!bSuccess) {}
        AsyncTask(
            ENamedThreads::GameThread,
            [this, bSuccess]() { OnConnectComplete.Broadcast(bSuccess); }
        );
      }
  );
}

void UVDA5050ClientComponent::Disconnect()
{
  if (Client)
  {
    Client->Disconnect();
  }
}

void UVDA5050ClientComponent::AcknowledgeNode(int32 SequenceId)
{
  if (Client)
  {
    Client->ClientNodeAck(static_cast<uint32_t>(SequenceId));
  }
}

void UVDA5050ClientComponent::ReportActionState(
    const FString& ActionId,
    const FString& ActionType,
    EVDA5050ActionStatus Status,
    const FString& ResultDescription
)
{
  if (Client)
  {
    Client->ReportActionState(
        TCHAR_TO_UTF8(*ActionId),
        TCHAR_TO_UTF8(*ActionType),
        static_cast<int>(Status),
        TCHAR_TO_UTF8(*ResultDescription)
    );
  }
}

void UVDA5050ClientComponent::TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction
)
{
  Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
  if (Client)
  {
    FVector Pos = GetOwner()->GetActorLocation();
    FRotator Rot = GetOwner()->GetActorRotation();
    double X = Pos.X / CM_TO_M;
    double Y = Pos.Y / CM_TO_M;
    double Theta = FMath::DegreesToRadians(Rot.Yaw);
    Client->ReportPose(X, Y, Theta);
  }
}
