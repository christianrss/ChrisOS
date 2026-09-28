# ChrisOS

<p align="center"><strong>Um sistema operacional experimental e plataforma de pesquisa em sistemas construída do kernel para cima.</strong></p>

<p align="center">
  <a href="https://os.christiansoftware.org/">Documentação</a> ·
  <a href="https://os.christiansoftware.org/en/">English</a> ·
  <a href="https://os.christiansoftware.org/pt-br/">Português</a> ·
  <a href="docs/README.md">Docs do repositório</a> ·
  <a href="CONTRIBUTING.md">Contribuição</a>
</p>

[English](README.md)

ChrisOS é um projeto de pesquisa e educação em sistemas operacionais centrado em um kernel x86-64 higher-half, desktop nativo, drivers, ChrisFS, ChrisC/CLVM, toolchain nativa, experimentos gráficos e a pilha de virtualização ChrisVM/ChrisCPU em desenvolvimento.

O repositório contém a implementação. A **documentação canônica do ChrisOS** fica em **https://os.christiansoftware.org/**. Este repositório mantém apenas documentação operacional de ambiente, build, execução, testes e contribuição.

> **Estado do projeto:** ChrisOS é experimental. QEMU é o principal ambiente de integração. Hardware físico ainda não é um alvo de implantação amplamente suportado.

## Início rápido

O ambiente de referência é Linux Debian/Ubuntu recente.

~~~bash
git clone https://github.com/christianrss/ChrisOS.git
cd ChrisOS

./scripts/check-dev-env.sh
make
make disk.img
make run
~~~

<code>make run</code> usa atualmente aceleração KVM. Para um smoke test headless com TCG, sem depender de KVM:

~~~bash
make test-qemu-ata
~~~

Consulte [montagem do ambiente](docs/getting-started/environment.md) e [build e execução](docs/getting-started/build-and-run.md).

## Componentes principais

| Área | Implementação |
| --- | --- |
| Kernel | x86-64 higher-half; memória, interrupções, SMP, processos e timers |
| Boot | Limine BIOS/UEFI |
| Armazenamento | ATA, AHCI, NVMe, VirtIO block, USB e ChrisFS |
| Gráficos | framebuffer, compositor 2D, 3D por software, VirtIO-GPU e VirGL |
| Desktop | gerenciador de janelas, barra, shell, editor e explorer |
| Linguagem | ChrisC, CLVM, interpretador e JIT |
| Toolchain nativa | ChrisAsm, ChrisO, ChrisLd e KCC |
| Rede | VirtIO-net e serviços mínimos no kernel |
| Virtualização | ChrisVM + ChrisCPU; ChrisHV é alvo arquitetural |
| Arquitetura secundária | bring-up RISC-V em QEMU <code>virt</code> |

## Arquitetura

~~~mermaid
flowchart TB
    FW["Firmware / Limine"] --> K["Kernel ChrisOS"]
    K --> M["CPU · memória · interrupções · SMP"]
    K --> FS["Storage + ChrisFS"]
    K --> GFX["Gráficos + window manager"]
    K --> NET["Rede"]
    K --> LANG["ChrisC / CLVM / JIT"]
    K --> TOOL["Toolchain + shell"]

    subgraph "Caminho de virtualização"
      OS["ChrisOS"] --> VHW["Hardware virtual"]
      VHW --> VM["ChrisVM"]
      VM --> CPU["ChrisCPU"]
      VM -. futuro .-> HV["ChrisHV VT-x/SVM"]
    end
~~~

Este diagrama é apenas uma orientação de alto nível. A arquitetura mantida está no [site oficial do ChrisOS](https://os.christiansoftware.org/).

## Comandos principais

| Objetivo | Comando |
| --- | --- |
| Gerar ISO | <code>make</code> ou <code>make iso</code> |
| Gerar kernel | <code>make kernel</code> |
| Criar disco ChrisFS | <code>make disk.img</code> |
| Executar ChrisOS x86-64 | <code>make run</code> |
| Testes host | <code>make host-gates</code> |
| Gates QEMU | <code>make qemu-gates</code> |
| Gate local amplo | <code>make full-gates</code> |
| Compilar ChrisVM | <code>make chrisvm</code> |
| Testar ChrisVM | <code>make chrisvm-test</code> |
| Bring-up RISC-V | <code>make riscv</code> / <code>make run-riscv</code> |
| Limpar artefatos | <code>make clean</code> |

## Documentação

Documentação operacional local:
- [Ambiente de desenvolvimento](docs/getting-started/environment.md)
- [Build e execução](docs/getting-started/build-and-run.md)
- [Fluxo de desenvolvimento](docs/development/workflow.md)
- [Testes](docs/development/testing.md)

Arquitetura, especificações, status, roadmaps, ChrisVM/ChrisCPU, toolchain, gráficos, filesystem, drivers, hardware, pesquisa e material educacional pertencem ao **https://os.christiansoftware.org/**.

## Licença

ChrisOS usa a [licença MIT](LICENSE).
