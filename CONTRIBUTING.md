# Contributing to ChrisOS

ChrisOS is an experimental systems project. Contributions should preserve reproducibility, architectural boundaries and evidence-based status claims.

## Read first

- [Repository documentation index](docs/README.md)
- [Architecture overview](docs/architecture/overview.md)
- [Development workflow](docs/development/workflow.md)
- [Testing and evidence](docs/development/testing.md)

Also read the architecture/status documents for the subsystem being modified.

## Setup

~~~bash
git clone https://github.com/christianrss/ChrisOS.git
cd ChrisOS
./scripts/check-dev-env.sh
make
~~~

The reference host is Debian/Ubuntu Linux. See [development environment](docs/getting-started/environment.md).

## Branches and commits

Create a topic branch from current <code>main</code>. Keep commits reviewable and scoped. Do not commit generated <code>build/</code> output, disk images, ISO images or test logs.

## Tests

Run the narrow test first, then the broadest practical gate.

Typical baseline:

~~~bash
make host-gates
~~~

For guest/hardware-facing changes, add the relevant QEMU target from <code>scripts/qemu.mk</code>.

For ChrisVM:

~~~bash
make chrisvm-test
~~~

If a relevant gate cannot be run, state that explicitly in the pull request.

## Code expectations

- Keep freestanding kernel constraints intact.
- Do not introduce host-libc assumptions into kernel code.
- Preserve explicit ownership and locking rules.
- Keep hardware/backend details behind existing abstractions where possible.
- Treat format/ABI changes as compatibility changes and document them.
- Prefer a targeted test over an unverified capability statement.
- Preserve warnings-as-errors behavior where the build already enforces it.

## Documentation expectations

Update documentation when a change modifies build requirements, commands, public ABIs/formats/protocols, subsystem boundaries or capability evidence.

The long-form site is https://os.christiansoftware.org/. Repository-local guides are authoritative for commands coupled to this source tree.

## Pull request checklist

- [ ] Clear and focused scope
- [ ] No generated artifacts
- [ ] Relevant host tests pass
- [ ] Relevant QEMU/ChrisVM gates pass or are explicitly listed as not run
- [ ] Architecture/ABI implications are documented
- [ ] Capability claims match the evidence level
- [ ] User/developer documentation is updated when required
