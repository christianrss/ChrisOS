# Repository-local documentation

The **canonical ChrisOS documentation is https://os.christiansoftware.org/**.

For end-to-end workstation setup, Windows/WSL2, build/debug, testing and contribution guidance, use the **[Developer Guide](https://os.christiansoftware.org/en/17-developer-guide/)**.

This directory is intentionally small. It contains only instructions that must stay close to the repository because they describe how to work with the current source tree.

## Kept here

### Getting started
- [Development environment](getting-started/environment.md)
- [Build and run](getting-started/build-and-run.md)

### Development
- [Development workflow](development/workflow.md)
- [Testing and evidence](development/testing.md)

Also see [Contributing](../CONTRIBUTING.md) and [Security policy](../SECURITY.md).

## Not maintained here

Architecture, subsystem design, ABIs, implementation-status snapshots, hardware matrices, audits, campaign reports, plans, roadmaps, ChrisVM/ChrisCPU documentation, toolchain documentation and educational chapters belong in **https://os.christiansoftware.org/** and its source repository, `christianrss/chrisos_site`.

This avoids two competing documentation trees and prevents historical status files from being mistaken for the current state.

## Rule

If a change affects setup, build, run, test or contribution mechanics, update these repository-local guides.

If a change affects architecture, behavior, specifications, subsystem documentation, capability status or project direction, update **chrisos_site**.
