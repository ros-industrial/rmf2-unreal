# VDA5050 Client Plugin
This module integrates the [vda5050_core library](https://github.com/ros-industrial/vda5050_core) as a `Component` for `Actor` classes.

## VDA5050Client Component

1. To add the `VDA5050Client Component` into an `Actor`, click on `Add` under the `Components` tab in the Actor Blueprint and search for `VDA5050Client`.

   ![UE5 Add Component](Images/VDA5050Client_Add_Component.png)

1. Upon succesfully adding the `Component` to an `Actor`,
the `VDA5050Client` Component will be visible under the `Components` tab.

   ![Client Component under Component Tab](Images/VDA5050Client_ClientComponent.png)


### Component Details

Under the `Details` tab of the `Component` (top right window), configure the following properties:


#### Connection

Used to construct the VDA5050 MQTT topic structure (`<InterfaceName>/<Version>/<Manufacturer>/<SerialNumber>/<topic>`) and establish the broker connection.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `BrokerAddress` | String | `tcp://localhost:1883` | MQTT broker address and port|
| `InterfaceName` | String | `""` | VDA5050 interface name, used as the first segment of the MQTT topic path |
| `Version` | String | `2.0.0` | VDA5050 protocol version. Determines the message schema and topic structure |
| `Manufacturer` | String | `Manufacturer` | AGV manufacturer identifier. Used in topic construction and state messages |
| `SerialNumber` | String | `""` | Unique AGV serial number. Each AGV instance must have a distinct value. **NOTE**: This is instance-editable, so it can be set per-actor in the level |
| `bAutoConnect` | Boolean | `false` | When enabled, the component will automatically connect to the broker on BeginPlay. If disabled, you must call `Connect()` manually from Blueprint. **NOTE**: If there are multiple actor instances of the blueprint in the level, it is best to disable this unless `SerialNumber` is set to instance-editable. |


#### Reporting Action State

Used to report current action state

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `ActionId` | String | `""` | Unique identifier of the action | 
| `ActionType` | String | `""` | Type of the action. Only for visualization purposes |
| `Status` | Enum | `""` | Current progression of the action `Waiting`, `Initializing`, `Running`, `Puased`, `Finished`, `Failed` |
| `ResultDescription` | String | `""` | The result of the action |

#### Automatic State Reporting

Once connected, the component publishes the AGV `state` message automatically — no Blueprint calls are required for these:

- **Position** — the owning actor's world transform is published as `agvPosition` every tick (converted from Unreal centimetres to metres).
- **Map** — `agvPosition.mapId` is adopted from whatever the master control sends: an `initPosition` instant action, or the `mapId` on incoming order nodes. The client never asserts its own map, so `mapId` is empty until the first order/localization arrives.
- **Connection** — `ONLINE` is published on connect, `OFFLINE` on `Disconnect`, and a `CONNECTIONBROKEN` last-will is delivered by the broker if the client drops uncleanly.

### Events and Blueprint Functions

#### Events

Bindable event dispatchers that notify the owning actor of VDA5050 events.
Can be added into the event graph by right-clicking the `VDA5050Client` in the `Components` tab and hovering over `Add Event`.
Events handle incoming orders and connection status.

![Adding Event to Event Graph](Images/VDA5050Client_Add_Event.png)

| Event | Parameter | Description |
|-------|-----------|-------------|
| `OnNodeDispatch` | `FVDA5050NodeInfo` | Fired when the next node in the current order is ready for execution. Contains the node ID, sequence ID, target position, and orientation. The target position can be further broken down into floating point variables `X`, `Y`, `Z`|
| `OnConnectComplete` | `bool bSuccess` | Fired when the MQTT connection attempt completes. `true` if connected successfully, `false` on failure |


#### Blueprint Functions

Callable functions for controlling the `VDA5050Client` from Blueprint.
Functions can be accessed by left-clicking and dragging the `VDA5050Client` variable pin onto the event graph:

![VDA5050Client Function List](Images/VDA5050Client_FunctionList.png)

| Function | Parameters | Description |
|----------|-----------|-------------|
| `Connect` | BrokerAddress, InterfaceName, Version, Manufacturer, SerialNumber | Establish an MQTT connection to the broker and subscribe to the order topic. All parameters are optional and will fall back to the component's configured defaults if not provided |
| `Disconnect` | - | Gracefully disconnect from the MQTT broker and clean up subscriptions |
| `AcknowledgeNode` | SequenceId | Acknowledge completion of a dispatched node. Call this after the AGV has reached the node's target position. This advances the order to the next node |
| `ReportActionState` | ActionId, ActionType, Status, ResultDescription | Report the status of an action into the AGV's published `state` message. Call it as the action progresses (e.g. `Running`, then `Finished` or `Failed`). Repeated calls with the same `ActionId` update the same entry rather than adding a duplicate. See [Reporting Action State](#reporting-action-state) for the parameters |


## Demonstration

Example of the `VDA5050Client` Component being used with `AI MoveTo`

![Example of VDAClient Blueprint Implementation](Images/VDA5050Client_EventGraph.png)
![Example of a Pickup Action State Reporting](Images/VDA5050Client_EventGraph_PickupAction.png)
