# Exceções

O monitor não encerra no primeiro vetor. `chris_raise` tenta entregar.

| Situação | Resultado |
| --- | --- |
| IDT com base 0 e limite 0 | `CHRIS_EXIT_EXCEPTION`, vetor salvo, sem triple fault |
| entrega falha e a IDT estava vazia | o mesmo, a exceção original |
| entrega falha com IDT carregada | nova tentativa como `#DF` |
| a segunda entrega falha | `CHRIS_EXIT_TRIPLE` e dump do anel de trace |

Prioridade já gerada pelos testes: `#UD`, `#GP`, `#PF`, `#DE`.

| Vetor | Quando |
| --- | --- |
| `#DE` | divisor zero ou quociente que não cabe |
| `#UD` | opcode não decodificado, `LOCK` em registrador, forma inválida |
| `#GP` | endereço não canônico, MSR desconhecido, CS inválido na entrega |
| `#PF` | entrada ausente ou sem permissão. `CR2` recebe o VA |

`#DB`, `#BP` como trap de depurador, `#DF` como vetor de origem, `#TS`, `#NP`, `#SS`, `#MF`, `#AC`, `#MC` e `#XM` têm constantes ou promoção parcial, mas não têm suíte própria. Um `PUSH` para página ausente é `#PF`, não `#SS`. A TSS e o IST ainda não escolhem pilha.

Triple fault não reseta a máquina em silêncio. O processo permanece parado com o anel recente impresso pelo log, se houver hook.
