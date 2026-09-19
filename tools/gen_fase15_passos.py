#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "learn" / "fase15-net64"
OUT.mkdir(parents=True, exist_ok=True)


def fence(name: str, path: Path) -> str:
    text = path.read_text(encoding="utf-8")
    return f"### `{name}`\n\n```c\n{text}```\n\n"


shell = (ROOT / "kernel/tools/shell.c").read_text(encoding="utf-8")

p01 = """# PASSO 01 — PCI scan + virtio-net legacy init

Tempo: 70 min. Anterior: [INDEX.md](INDEX.md) · Próximo: [PASSO_02.md](PASSO_02.md)

## Objetivo

Escanear PCI bus 0, achar `0x1AF4:0x1000` (virtio-net legacy), habilitar I/O + bus master, negociar features e ler MAC. Serial: `virtio-net ready mac=...`.

## Makefile

Adicione `-Ikernel/net`, `kernel/metal/pci.o`, `kernel/net/virtio_net.o`, `kernel/net/net.o` em `C_OBJECTS`, e a regra:

```make
kernel/net/%.o: kernel/net/%.c
\t$(CC) $(CFLAGS) -c $< -o $@
```

## Arquivos novos

""" + fence("kernel/metal/pci.h", ROOT / "kernel/metal/pci.h") + fence(
    "kernel/metal/pci.c", ROOT / "kernel/metal/pci.c"
) + fence("kernel/net/virtio_net.h", ROOT / "kernel/net/virtio_net.h") + fence(
    "kernel/net/virtio_net.c", ROOT / "kernel/net/virtio_net.c"
) + """## Gate

`make` sem warnings. QEMU com `-device virtio-net-pci,netdev=n0 -netdev user,id=n0` imprime MAC na serial.

Tag: `net64-01`.
"""

p02 = """# PASSO 02 — filas TX/RX virtio

Tempo: 80 min. Anterior: [PASSO_01.md](PASSO_01.md) · Próximo: [PASSO_03.md](PASSO_03.md)

## Objetivo

Virtqueues legacy (RX fila 0, TX fila 1), buffers `pmm_alloc`, `virtio_net_tx` e `virtio_net_poll`. Serial `tx bytes=N` ao transmitir.

Substitua `kernel/net/virtio_net.c` por inteiro:

""" + fence("kernel/net/virtio_net.c", ROOT / "kernel/net/virtio_net.c") + """Tag: `net64-02`.
"""

p03 = """# PASSO 03 — Ethernet + ARP

Tempo: 50 min. Anterior: [PASSO_02.md](PASSO_02.md) · Próximo: [PASSO_04.md](PASSO_04.md)

## Objetivo

IP estático `10.0.2.15/24`, responder ARP request. Serial `arp reply`.

""" + fence("kernel/net/net.h", ROOT / "kernel/net/net.h") + fence(
    "kernel/net/net.c", ROOT / "kernel/net/net.c"
) + """Em `kernel/metal/start.c`, após `speaker_off()`:

```c
#include "net.h"
...
if (!net_init()) {
    serial_puts("ChrisOS: net unavailable\\n");
}
```

Em `kernel/wm/main.c`, no loop:

```c
#include "net.h"
...
net_poll();
```

Tag: `net64-03`.
"""

p04 = """# PASSO 04 — UDP echo porta 7

Tempo: 50 min. Anterior: [PASSO_03.md](PASSO_03.md) · Próximo: [PASSO_05.md](PASSO_05.md)

## Objetivo

IPv4 mínimo + UDP destino 7 ecoa payload (checksum UDP pode ser 0). Serial `udp echo`.

Substitua `kernel/net/net.c` por inteiro (já inclui ARP + UDP):

""" + fence("kernel/net/net.c", ROOT / "kernel/net/net.c") + """Tag: `net64-04`.
"""

p05 = """# PASSO 05 — gate QEMU + comando `net`

Tempo: 30 min. Anterior: [PASSO_04.md](PASSO_04.md) · Próximo: [PASSO_06.md](PASSO_06.md)

## Objetivo

`make run` com virtio-net e hostfwd UDP. Shell comando `net` mostra MAC/IP.

### Alvo `run` do Makefile

```make
run: $(ISO)
\t$(QEMU) -M q35 -m 256M -boot d -cdrom $(ISO) \\
\t\t-drive file=disk.img,format=raw,if=ide,index=0,media=disk \\
\t\t-device virtio-net-pci,netdev=n0 \\
\t\t-netdev user,id=n0,hostfwd=udp::7007-:7 \\
\t\t-serial stdio -no-reboot -no-shutdown
```

### Host

```sh
echo -n ping | ncat -u -w 1 127.0.0.1 7007
```

Deve voltar `ping`. Serial guest `udp echo`.

Substitua `kernel/tools/shell.c` por inteiro:

```c
""" + shell + """```

Tag: `net64-05`.
"""

p06 = """# PASSO 06 — TCP echo mínimo (SYN/ACK + payload)

Tempo: 60 min. Anterior: [PASSO_05.md](PASSO_05.md)

## Objetivo

TCP porta 7: SYN → SYN-ACK, PSH+ACK ecoa payload. Uma conexão por vez. Serial `tcp syn-ack`, `tcp echo`.

Atualize `run` com `hostfwd=tcp::7007-:7`:

```make
-netdev user,id=n0,hostfwd=udp::7007-:7,hostfwd=tcp::7007-:7
```

Substitua `kernel/net/net.c` por inteiro:

""" + fence("kernel/net/net.c", ROOT / "kernel/net/net.c") + """### Host

```sh
echo -n hello | ncat -w 2 127.0.0.1 7007
```

Deve voltar `hello`. Serial: `tcp syn-ack` depois `tcp echo`.

## Limitações (fase 15)

- virtio-net **legacy PCI I/O BAR** apenas (sem virtio-pci modern MMIO).
- Uma conexão TCP; sem FIN/RST/retransmit.
- Sem fragmentação IP, sem ICMP, sem DHCP.
- Poll por frame do desktop (sem IRQ dedicada).
- UDP checksum enviado como 0.

Tag: `net64-06`.
"""

index = """# Fase 15 — virtio-net + UDP/TCP echo

Anterior: [JIT](../fase14-jit64/INDEX.md). Próximo: fase 16 (TCP completo).

Protocolo fechado: virtio-net PCI legacy + Ethernet + ARP + UDP echo porta 7 + TCP echo mínimo porta 7.

## Ordem

| Passo | Entrega | Gate |
|------:|---------|------|
| [01](PASSO_01.md) | PCI + virtio init | MAC na serial |
| [02](PASSO_02.md) | TX/RX rings | `tx bytes=N` |
| [03](PASSO_03.md) | ARP | `arp reply` |
| [04](PASSO_04.md) | UDP echo 7 | `udp echo` |
| [05](PASSO_05.md) | QEMU + shell `net` | `ncat -u` ecoa |
| [06](PASSO_06.md) | TCP SYN/ACK echo | `ncat` TCP ecoa |

Comece em [PASSO_01.md](PASSO_01.md).
"""

for name, body in [
    ("PASSO_01.md", p01),
    ("PASSO_02.md", p02),
    ("PASSO_03.md", p03),
    ("PASSO_04.md", p04),
    ("PASSO_05.md", p05),
    ("PASSO_06.md", p06),
    ("INDEX.md", index),
]:
    (OUT / name).write_text(body, encoding="utf-8")
    print(f"wrote {name} ({len(body)} bytes)")
