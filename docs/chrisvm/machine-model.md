# Modelo da máquina

`ChrisMachine` é o guest. Ela guarda a configuração, a RAM, os slots de I/O e MMIO, a serial, o framebuffer, o ponteiro de CPU e o backend.

A configuração vem de `chris_config_init` e de `chris_config_from_args`:

```text
chrisvm [--backend=chriscpu] [--trace] [--trace-memory] [--trace-io] [--trace-mmio]
       [--deterministic] [--debug] [--headless] [--fb-dump=PATH] [--break=ADDR] [--max-steps=N] guest.elf
```

`--deterministic` é aceito e já é o comportamento padrão: não há timer nem entropia. O desenho deixa o relógio e as entradas externas atrás da máquina, para um record/replay futuro não precisar reescrever o executor.

## Ciclo de vida

1. `chris_machine_create` valida a RAM, chama `backend->init` e `create_cpu`, zera a RAM, conecta a serial e aloca o framebuffer.
2. `chris_load_elf` copia os segmentos.
3. `chris_boot` instala o protocolo v1 e publica o estado pela interface `set_state`.
4. `chris_run` chama `backend->run`.
5. `chris_machine_destroy` chama `shutdown`.

`init` do ChrisHV falha. A máquina não é criada e nenhum dispositivo chega a rodar num backend inexistente.

## Dispositivos

Há um dispositivo serial, a porta de shutdown e um framebuffer linear. Serial e shutdown se registram no barramento de portas. O framebuffer é uma faixa física copiada por inteiro, fora da tabela de MMIO. O limite atual é 8 intervalos de I/O e 8 regiões MMIO. Isso cabe na fundação; PCI e VirtIO vão precisar de uma tabela maior, sem mudar o formato do callback.

O frontend só observa. A serial pode ter um hook de caractere. O log do trace é outro hook. `--fb-dump` grava PNG quando o caminho termina em `.png`, e PPM nos demais casos. Sem `--headless`, uma tela suja abre uma janela SDL intitulada `ChrisOS` por quatro segundos. Nenhum desses caminhos altera registradores.
