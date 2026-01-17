RoboEyes (vendor placeholder)

This directory is intended to contain the RoboEyes library source (FluxGarage RoboEyes) and Adafruit GFX dependency.

IMPORTANT:
- RoboEyes is licensed under GPL-3.0. Including it in the firmware requires complying with GPL-3 obligations.
- To vendor RoboEyes, download the repository and place the `src/` files here.

Placement instructions:
1. git clone https://github.com/FluxGarage/RoboEyes into this directory or copy the `src/` and `examples/` folders.
2. Add Adafruit GFX sources under `main/third_party/adafruit_gfx/` or as a managed component.
3. Update `main/CMakeLists.txt` to include RoboEyes sources if present.

