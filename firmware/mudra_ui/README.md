# MUDRA AI TFT UI

## UI design specification

Portrait 240x320 logical canvas (placeholder until measured). Near-black background, charcoal cards, cyan primary accent, violet secondary accent, green success and amber waiting state. Screens use a 12 px margin, large 24–32 px title/name scale, concise status labels and minimum 42 px action targets. Detection prioritizes mudra name, confidence, bar, result state and camera/model indicators.

## Architecture and current limits

`include/ui_model.h` holds view state and abstract touch actions. `include/display_backend.h` defines rendering primitives; `NullDisplay` makes firmware logic buildable before a controller driver exists. `include/hardware_config.h` isolates controller, dimensions, rotation and the proposed GPIO map. `src/main.cpp` implements Splash, Home, Detection, Settings and About, plus JSON serial handling. The example firmware currently uses a no-op display backend: it compiles UI logic but cannot draw until a driver adapter is selected.

Controller `HC028Q42` is treated only as a module marking, not an LCD or touch IC identification. Screen dimensions/orientation, LCD controller, touch controller and its wiring must be verified before hardware integration. Proposed GPIOs are copied from the supplied mapping and are not certified as electrically compatible. The sketch does not implement SD or brightness control. Touch events and hit targets are represented in the model but physical input awaits a replaceable touch-driver adapter.

## Protocol

USB serial 115200 baud, UTF-8 JSON, one object per newline, maximum receive line 512 bytes. Laptop to device: `connection`, `camera`, `model`, and `detection` events. A detection event carries `mudra`, `confidence` (0–100), and `status` (`recognized`, `waiting`, `no_hand`, or `model_disconnected`). Device-to-laptop commands are newline-delimited `START_DETECTION`, `STOP_DETECTION`, and `GET_STATUS`; the hardware-independent navigation model can map button actions to these commands once touch integration is added. Five-second silence marks laptop/camera/model disconnected.

## Build and preview

Install PlatformIO Core and ArduinoJson in the PlatformIO project environment, then run `pio run -d firmware/mudra_ui`. This does not flash the board. Install `pyserial` and run `python firmware/mudra_ui/tools/serial_demo.py --port COM9` only when the ESP32 is running compatible firmware; the script owns COM9 while open. Do not run it alongside another serial monitor.

Desktop simulator: the Python model/UI simulator is `tools/ui_simulator.py`; it has no serial or display-driver dependency.

## Hardware-independent test checklist

- Verify parser accepts each event and ignores malformed JSON and overlong lines.
- Confirm timeout changes connection indicators after five seconds.
- Confirm all five screen layouts fit 240x320 in the desktop simulator.
- Exercise touch navigation in simulation and verify START/STOP command output.
- Compile firmware against the NullDisplay backend; hardware rendering and touch remain unverified until exact ICs/wires are identified.
