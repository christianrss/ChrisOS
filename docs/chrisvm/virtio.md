# VirtIO

Estado: `NOT STARTED`.

A ordem prevista, reusando os drivers do ChrisOS, é block, input, GPU, net, rng e, depois, sound. Existe um único dispositivo por tipo, compartilhado pelos backends. ChrisCPU chega nele por MMIO traduzido. ChrisHV, no futuro, chega por violação de EPT ou NPT e o mesmo callback.

VirtIO-GPU e VirGL do kernel continuam no caminho QEMU. O ChrisVM não os reimplementa nesta rodada e não os remove.
