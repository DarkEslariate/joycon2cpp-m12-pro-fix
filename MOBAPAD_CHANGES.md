# Mobapad M12 Pro changes

This file documents the Mobapad M12 Pro compatibility work in this fork of
[TheFrano/joycon2cpp](https://github.com/TheFrano/joycon2cpp).

Maintained by [@DarkEslariate](https://github.com/DarkEslariate).

## What changed

The M12 Pro uses a different BLE input path from the standard Switch 2 Joy-Con path expected by the original application. This fork adds a separate handling path for that controller behavior:

- Added the session characteristic and side-specific native input characteristic identifiers used by the M12 Pro.
- Added session setup and notification subscription for the native input characteristic.
- Added controller discovery handling that can accept third-party controller product IDs instead of requiring only Nintendo's known product IDs.
- Added handling to route native console-style input reports into the application's existing input processing.
- Added M12 Pro IMU quaternion decoding and conversion into gyro data for the existing motion pipeline.
- Added the controller's vibration characteristic handling so rumble can be sent through the M12 Pro path.

## Features retained

The intent of the fork is to add M12 Pro compatibility without removing the original joycon2cpp functionality. The original Switch 2 Joy-Con support remains, including:

- Button and stick inputs
- Rumble
- Gyro / motion input

The application continues to use the original project's virtual-controller and output pipeline.

## Scope

These changes are focused on compatibility with the Mobapad M12 Pro. They do not represent a complete rewrite of joycon2cpp, and the upstream project remains the source of the original application and Switch 2 controller implementation.

For setup, dependencies, and build instructions, see [README.md](README.md).
