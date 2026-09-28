# Security policy

ChrisOS is an experimental operating-system and systems-research project. It is not presented as a hardened production operating system.

## Supported code

Security fixes should target the current <code>main</code> branch unless a maintainer explicitly identifies another supported branch.

## Reporting a vulnerability

Do not publish exploit details in a normal public issue before a maintainer has had a reasonable opportunity to assess them.

If GitHub private vulnerability reporting is available in the repository Security tab, use that channel. Otherwise, contact the maintainer through the contact method exposed on the maintainer's GitHub profile.

Include the affected commit, subsystem, reproduction conditions, impact, a minimal proof of concept when safe, and whether the issue is reachable only in QEMU/test environments or also in a plausible hardware deployment.

## Scope

Useful reports include memory-safety errors, privilege-boundary failures, filesystem corruption paths, malformed executable/image handling, network parser issues and virtualization-boundary problems.

Because ChrisOS is experimental, a missing hardening feature by itself is not necessarily a vulnerability. Reports are most useful when they identify a concrete violation of an intended boundary or a reproducible security impact.
