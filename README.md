# Manvil

Experimental project not completed, may not continue
A native Vulkan driver for Arm Mali Valhall GPUs running on the proprietary
Kbase kernel driver.

## Overview

Manvil is an open source implementation of a Vulkan ICD (Installable Client
Driver) that communicates directly with the Kbase kernel driver through its
CSF (Command Stream Frontend) interface. It targets Mali Valhall GPUs on
Android and ARM Linux systems where the proprietary Kbase stack is available.

The project is developed independently and does not derive from Mesa,
Panfrost, PanVK, or any other existing driver implementation. All protocols,
data structures, and interfaces are designed from the publicly available
Kbase UAPI headers, published security advisories, and empirical observation
of the hardware behavior.

## Goals

- Provide a clean, documented, and maintainable Vulkan driver for Mali
  Valhall GPUs on Kbase.
- Support multiple Kbase UAPI versions, starting with 1.20 and 1.38.
- Remain independent from any specific graphics framework or Mesa version.
- Serve as a reference implementation for the Kbase CSF interface.

## Status

Early development. The project is currently in the documentation and
architecture phase. No functional code exists yet.

## Target Hardware

- Mali-G615 MC6 (MediaTek Dimensity series)
- Additional Valhall GPUs to be validated as development progresses

## Requirements

- AArch64 Linux environment with access to `/dev/mali0`
- CMake 3.20 or newer
- GCC 12 or newer, or Clang 15 or newer
- Rust 1.70 or newer (for tooling)

## Building

Build instructions will be provided once the initial implementation is ready.

## Documentation

See the `docs/` directory for architecture, design decisions, and interface
specifications.

## License

MIT License. See `LICENSE` for details.

## Author

Noin Haxel
Mecca OpenSource
