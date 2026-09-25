# Boot direto

O texto versionado do contrato está em `docs/chrisvm-boot-protocol.md`.

Resumo da versão 1:

- ELF64 x86-64 com endereços físicos baixos;
- RIP = entry, RSP = `0x80000`;
- identidade de 2 MiB, long mode, GDT em `0x70000`;
- CS `0x08`, dados `0x10`;
- serial `0x3F8`, shutdown `0x501`;
- ELF em `0xffff800000000000` ou acima é recusado.

Não há boot info. Não há atalho por RIP. O próximo protocolo é que vai descrever o higher-half do ChrisOS, em vez de o interpretador reconhecer `kstart`.
