# Manvil Architecture

## Layer Overview

Manvil is organized into five layers, each depending only on the layer
immediately below it.

Layer 1, Hardware, is the Kbase kernel driver exposed through the device
node /dev/mali0.

Layer 2, Device and Kernel API, owns the connection to the kernel driver
and exposes a stable interface to the layers above.

Layer 3, CSF and Memory, implements the Command Stream Frontend protocol
and the GPU memory model.

Layer 4, Scheduling and Shaders, converts high-level operations into the
command sequences consumed by the firmware.

Layer 5, Vulkan ICD, implements the Vulkan entry points required by the
Vulkan loader.

## Layer Descriptions

### Hardware Layer

The Kbase kernel driver exposes its interface through /dev/mali0. All
communication occurs through ioctl calls, memory mappings, and shared
command rings.

### Device and Kernel API Layer

This layer owns the connection to the kernel driver. It performs the
initial handshake, negotiates the Kbase UAPI version, discovers the GPU
model and capabilities, and exposes a stable interface to the layers above.

The ABI subdirectory contains the complete set of ioctl numbers, data
structures, and constants for each supported Kbase version. Every value
in this directory is either taken from published UAPI headers or derived
from documented behavior of the kernel driver.

### CSF and Memory Layer

This layer implements the Command Stream Frontend protocol. It manages
command stream groups, command queues, ring buffers, synchronization
objects, and the tiler heap. It also provides memory allocation and
mapping services through the Kbase memory model.

The KCPU submodule implements the ten kernel-side commands used to control
the firmware, including command queue state updates, fence waits, error
barriers, memory import, shader JIT operations, and group suspension.

### Scheduling and Shaders Layer

This layer converts high-level operations into the command sequences
consumed by the firmware. It handles submission ordering, timeline
management, shader loading and caching, and command buffer construction.

### Vulkan ICD Layer

This layer implements the Vulkan entry points required by the Vulkan
loader. It exposes instances, physical devices, logical devices, queues,
command buffers, and all associated Vulkan objects. It performs no direct
kernel communication. All hardware interaction is delegated to the layers
below.

## Directory Structure

The include directory contains public headers under include/manvil.

The src directory contains all implementation code, subdivided by layer:
src/kernel_api, src/device, src/mem, src/csf, src/kcpu, src/heap,
src/shader, src/firmware, src/sched, src/cmd, src/regs, src/wsi, and
src/vulkan.

The regs_data directory contains register description data used to
generate register headers.

The shaders_precomp directory contains precompiled built-in shaders.

The tools directory contains development and diagnostic utilities.

The tests directory contains unit, integration, and Vulkan tests.

The scripts directory contains build, deploy, and extraction scripts.

The docs directory contains design and interface documentation.

The reference directory contains notes derived from public sources.

## Data Flow

A Vulkan command submission follows this path through the layers: the
application calls vkQueueSubmit, the Vulkan ICD layer receives the call,
the scheduling layer orders it, the command buffer construction step
produces the command sequence, the CSF layer writes it into a ring
buffer, the KCPU command dispatch is invoked, an ioctl is issued to
/dev/mali0, the Kbase kernel driver receives it, and finally the CSF
firmware on the GPU executes the work.

Completion notifications follow the reverse path, with synchronization
objects updated either by the firmware directly or by the kernel.

## Versioning Strategy

The ABI layer supports multiple Kbase UAPI versions. Each version has its
own set of ioctl numbers, structure layouts, and constants. The driver
selects the appropriate version during the device handshake and routes all
subsequent calls through that version.

Adding support for a new Kbase version requires creating a new ABI
definition file and updating the version table in the kernel API layer.
No changes are required in higher layers.
