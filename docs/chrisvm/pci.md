# PCI

Estado: `NOT STARTED`.

`buses/pci/pci.h` só reserva o lugar. Não há configuração, BAR, IRQ de pino nem dispositivo. A especificação v1 deixa a topologia em aberto de propósito: o mapa estável sai quando o primeiro dispositivo VirtIO precisar de um bus, para não inventar IDs que o ChrisOS não usa.

O kernel hoje fala com VirtIO atrás do QEMU. Essa topologia é a referência a copiar, não uma razão para criar um PCI paralelo por backend de CPU.
