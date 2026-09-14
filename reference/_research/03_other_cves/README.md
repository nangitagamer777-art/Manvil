# Other CVE Research

Additional Mali GPU kernel driver disclosures beyond CVE-2023-6241.
Each one is analyzed for interface definitions, version information, or
behavioral details that are useful for Manvil.

## Package inventory

cve-2023-48409   KCPU queue integer overflow, leaked KCPU queue size
cve-2025-8045    KCPU queue UAF through cpu_queue_dump, timer details
ghsl-advisories  Google SecurityLab writeups for CVE-2025-0072 and 0073
cve-2023-26083   Timeline stream kernel pointer leak

## Value summary

The most useful findings are recorded in the per-CVE documents.
The following items are directly usable for Manvil:

  KBASE_API_VERSION encoding as a single u32.
  KCPU timer bucket size of 256 ms.
  CQS_WAIT can block indefinitely, so timeouts are mandatory.
  CS_CPU_QUEUE_DUMP typical buffer size of 60 KiB.
  Timeline stream packet format and parse pattern.
  VERSION_CHECK ioctl differences between JM (0) and CSF (52).
  The BASE_CONTEXT_CREATE_FLAG_MONITOR bit for timeline access.

Items that are only useful as historical context or as confirmation of
already-known facts are also recorded, but marked as low priority.

## Non-reusable content

All exploit payloads, kernel symbol offsets, and target-specific
constants are excluded. Only interface definitions and behavioral
patterns are recorded.
