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

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"

#include "VDA5050ClientComponent.generated.h"

class FVDA5050Client;

USTRUCT(BlueprintType)
struct RMF2RUNTIME_API FVDA5050NodeInfo
{
  GENERATED_BODY()

  UPROPERTY(BlueprintReadOnly, Category = "VDA5050")
  FString NodeId;

  UPROPERTY(BlueprintReadOnly, Category = "VDA5050")
  int32 SequenceId = 0;

  UPROPERTY(BlueprintReadOnly, Category = "VDA5050")
  FVector Position = FVector::ZeroVector;

  UPROPERTY(BlueprintReadOnly, Category = "VDA5050")
  float Theta = 0.0f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnNodeDispatch,
    const FVDA5050NodeInfo&,
    Node
);

// Action execution status enum, maps directly to VDA_core's action status
UENUM(BlueprintType)
enum class EVDA5050ActionStatus : uint8
{
  Waiting,
  Initializing,
  Running,
  Paused,
  Finished,
  Failed
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnConnectComplete, bool, bSuccess);

UCLASS(ClassGroup = (VDA5050), meta = (BlueprintSpawnableComponent))
class RMF2RUNTIME_API UVDA5050ClientComponent : public UActorComponent
{
  GENERATED_BODY()

public:
  UVDA5050ClientComponent();

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VDA5050|Connection")
  FString BrokerAddress = TEXT("tcp://localhost:1883");

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VDA5050|Connection")
  FString InterfaceName = TEXT("");

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VDA5050|Connection")
  FString Version = TEXT("2.0.0");

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VDA5050|Connection")
  FString Manufacturer = TEXT("Manufacturer");

  UPROPERTY(
      EditInstanceOnly,
      BlueprintReadWrite,
      Category = "VDA5050|Connection"
  )
  FString SerialNumber = TEXT("");

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VDA5050|Connection")
  bool bAutoConnect = false;

  UPROPERTY(BlueprintAssignable, Category = "VDA5050|Events")
  FOnNodeDispatch OnNodeDispatch;

  UPROPERTY(BlueprintAssignable, Category = "VDA5050|Events")
  FOnConnectComplete OnConnectComplete;

  UFUNCTION(
      BlueprintCallable,
      Category = "VDA5050",
      meta =
          (DisplayName = "Connect VDA5050Client",
           Keywords = "Establish client connection")
  )
  void Connect(
      const FString& InBrokerAddress = "tcp://localhost:1883",
      const FString& InInterfaceName = "",
      const FString& InVersion = "2.0.0",
      const FString& InManufacturer = "Manufacturer",
      const FString& InSerialNumber = ""
  );

  UFUNCTION(
      BlueprintCallable,
      Category = "VDA5050",
      meta =
          (DisplayName = "Disconnect VDA5050Client",
           Keywords = "Disconnect client")
  )
  void Disconnect();

  UFUNCTION(
      BlueprintCallable,
      Category = "VDA5050",
      meta =
          (DisplayName = "Acknowledge Node",
           Keywords = "Acknowledge VDA Node Completion")
  )
  void AcknowledgeNode(int32 SequenceId);

  UFUNCTION(
      BlueprintCallable,
      Category = "VDA5050",
      meta =
          (DisplayName = "Report Action State",
           Keywords = "Report Action Status")
  )
  void ReportActionState(
      const FString& ActionId,
      const FString& ActionType,
      EVDA5050ActionStatus Status,
      const FString& ResultDescription = ""
  );

protected:
  virtual void BeginPlay() override;

  virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
  virtual void TickComponent(
      float DeltaTime,
      ELevelTick TickType,
      FActorComponentTickFunction* ThisTickFunction
  ) override;

private:
  TSharedPtr<FVDA5050Client> Client;
};
