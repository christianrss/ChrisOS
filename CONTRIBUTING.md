# Contributing to ChrisOS

ChrisOS is an experimental systems project. Contributions should preserve reproducibility, architectural boundaries and evidence-based status claims.

## Read first

- **Canonical technical documentation:** https://os.christiansoftware.org/
- **Developer Guide:** https://os.christiansoftware.org/en/17-developer-guide/
- [Repository operational documentation](docs/README.md)
- [Development workflow](docs/development/workflow.md)
- [Testing and evidence](docs/development/testing.md)

## Setup

~~~bash
git clone https://github.com/christianrss/ChrisOS.git
cd ChrisOS
./scripts/check-dev-env.sh
make
~~~

The reference host is Debian/Ubuntu Linux. Windows contributors should use WSL2. See [development environment](docs/getting-started/environment.md) and the canonical [Developer Guide](https://os.christiansoftware.org/en/17-developer-guide/).

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

The canonical documentation source is `christianrss/chrisos_site`, published at https://os.christiansoftware.org/.

Do **not** add architecture, status, audit, roadmap, specification, or subsystem-documentation files to this repository.

Update repository-local documentation only for environment, build, run, test, and contribution mechanics. Changes to architecture, interfaces, subsystem behavior, implementation status, or project direction belong in `chrisos_site`.

## Pull request checklist

- [ ] Clear and focused scope
- [ ] No generated artifacts
- [ ] Relevant host tests pass
- [ ] Relevant QEMU/ChrisVM gates pass or are explicitly listed as not run
- [ ] Architecture/ABI implications are documented
- [ ] Capability claims match the evidence level
- [ ] Canonical documentation in `chrisos_site` is updated when required
