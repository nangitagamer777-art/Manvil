# Manvil Overview

## Purpose

Manvil is a native Vulkan driver for Arm Mali Valhall GPUs that operate on
the proprietary Kbase kernel driver. It provides an Installable Client Driver
(ICD) for Vulkan applications running in environments where the standard
DRM-based Panfrost or Panthor kernel interfaces are not available.

## Motivation

Platforms running Android or ARM Linux with Mali GPUs typically ship with the
proprietary Kbase kernel driver rather than the upstream Panfrost or Panthor
DRM drivers. Applications wishing to use Vulkan on these platforms must rely
on the vendor-provided Vulkan library, which is closed source and not
available for arbitrary environments such as proot, chroot, or custom
container setups.

Manvil addresses this gap by providing an independent Vulkan implementation
that interfaces with Kbase directly, without relying on the proprietary user
space components.

## Design Principles

1. Independence. Manvil does not derive from Mesa, Panfrost, PanVK, or any
   other existing driver. All interfaces are designed based on publicly
   available documentation and empirical observation.

2. Layered architecture. The driver is organized into strict layers, from
   the low-level kernel interface up to the Vulkan ICD. Each layer has a
   well-defined public API and does not bypass its immediate neighbors.

3. Explicit versioning. The driver supports multiple Kbase UAPI versions
   through a centralized ABI layer. Adding support for a new version does
   not require modifications outside that layer.

4. Documentation as code. Design decisions, interface specifications, and
   known limitations are recorded in the docs directory and remain in sync
   with the implementation.

## Scope

Manvil targets the CSF (Command Stream Frontend) variant of Kbase, which is
used by Valhall and newer Mali GPU architectures. The Job Manager (JM)
variant used by Midgard and Bifrost is outside the current scope.

## Relationship to Other Projects

Manvil shares objectives with several existing open source graphics drivers:

- NVK provides a native Vulkan driver for Nvidia GPUs.
- Asahi provides OpenGL and Vulkan drivers for Apple Silicon.
- Freedreno provides drivers for Qualcomm Adreno GPUs.

These projects demonstrate that a graphics driver can be developed from
public documentation and empirical observation. Manvil applies the same
approach to Mali Valhall on Kbase.

## License

Manvil is released under the MIT License. See the LICENSE file at the
repository root.
