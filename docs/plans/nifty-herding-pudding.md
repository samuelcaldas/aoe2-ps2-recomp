# Plano: concluir e validar EE RAM de 64 MB no fork dedicado

## Contexto

A expansão permite endereçar buffers maiores, preparando a futura ampliação da viewport sem portar toda a camada de memória da versão PC. O usuário determina alterações diretamente em `third_party/PS2Recomp` (`samuelcaldas/PS2Recomp`), sem séries de patches.

A inspeção atual confirma que a mudança básica já existe no fork, em `dfae1cb9d0c02d52e027fa0d3c533638b8fca988`: RAM de 64 MB, máscara derivada, máscaras SIF atualizadas e heap/stacks relativos ao novo teto. O repositório principal também define 64 MB; lockfile e manifesto apontam para esse SHA. Não repetir essas mudanças nem declarar conclusão apenas pelas constantes.

**Tamanho recomendado:** manter 64 MiB (`0x04000000`, máscara `0x03FFFFFF`). Não introduzir configuração de tamanho nem elevar para 128 MiB sem necessidade demonstrada. O espaço adicional não é automaticamente uma reserva exclusiva do renderizador.

## Escopo e restrições

- Concluir a segurança, reprodutibilidade e validação da expansão já implementada.
- Alterações de runtime diretamente no código do fork, preservando GPL-3.0; nenhum patch novo.
- Não ampliar viewport, realocar playfield, implementar HUD HD ou substituir o alocador do jogo nesta tarefa.
- Não editar código gerado nem versionar binários, mapas de funções ou projetos Ghidra.
- Preservar alterações alheias, especialmente `platform/common/src/game_logging.cpp`. Sem worktrees, PRs ou push.

## 1. Regressões de acesso à RAM

Reutilizar os testes existentes de `GuestMemory` e do runtime, acrescentando cobertura comportamental antes de corrigir eventuais lacunas:

- Escrita/leitura acima de 32 MiB sem alterar o endereço correspondente na metade inferior.
- Primeiro endereço da metade superior e último intervalo válido, incluindo acessos multibyte próximos ao teto.
- Aliases KSEG0/KSEG1 para a metade superior pelos caminhos reais de tradução; preservar I/O e scratchpad.
- Rejeição de intervalos inválidos nos wrappers com bounds checking, inclusive tamanho que causaria overflow; distinguir isso do mascaramento intencional das macros guest.
- Caminhos de DMA/SIF que transferem dados para a metade superior, usando as APIs/testes existentes.

Arquivos centrais: `ps2xRuntime/include/runtime/ps2_memory.h`, `ps2xRuntime/src/lib/ps2_memory.cpp`, `ps2xRuntime/include/ps2_runtime.h`, `ps2xRuntime/include/ps2_runtime_macros.h` no fork; `runtime/include/aoe2/runtime/guest_memory.hpp` e diagnósticos `game_hooks/src/*_snapshot.hpp` no principal. Reutilizar helpers de acesso existentes em vez de criar outra camada de memória.

## 2. Tornar explícito o layout heap/HLE/stacks

Revisar os consumidores de `kGuestHeapHardLimit`, os pools RPC/TLS/BootMode em `ps2xRuntime/src/lib/Kernel/Syscalls/Helpers/State.h` e a alocação de callback stacks em `ps2_runtime.cpp`.

O plano anterior pressupunha que pools próximos de 31 MiB eram seguros, mas ampliar o heap até 63 MiB invalida essa justificativa. Reservar explicitamente os 256 KiB imediatamente abaixo do MiB de stacks para HLE: base `PS2_RAM_SIZE - 0x00140000u`, pools com os deslocamentos atuais `+0`, `+0x10000`, `+0x20000`, `+0x30000`. O limite exclusivo do heap passa a ser essa base; o piso das stacks permanece `PS2_RAM_SIZE - 0x00100000u`, independente do limite do heap. Centralizar essas constantes no header de layout/memória existente e usá-las em `State.h`, `ps2_runtime.h` e `ps2_runtime.cpp`. Conferir tamanhos e todos os consumidores dos pools antes da mudança e rejeitar carregamento/alocação que invada reservas.

Reutilizar `ps2xTest/src/ps2_memory_tests.cpp` e `ps2xTest/src/ps2_runtime_expansion_tests.cpp` para testes de não sobreposição, limites de alocação e preservação dos pools após alocações grandes. Documentar que um futuro buffer de mapa precisará de reserva/alocação explícita: não prometer que toda a metade superior está livre. Não mover o playfield nesta etapa.

## 3. Proveniência e fluxo sem patches

- Em `tests/test_generation_manifest.py`, substituir SHAs históricos nas fixtures válidas pelo SHA obtido do lockfile. Preservar/adicionar teste negativo que rejeite um SHA divergente; não enfraquecer o verificador.
- Em `tools/bootstrap_dependencies.py`, `PATCH_DIRECTORIES` já está vazio: não há aplicação ativa de patches ao fork. Atualizar a docstring obsoleta e acrescentar cobertura de que PS2Recomp é obtido diretamente do fork no SHA fixado sem série de patches. Não apagar ferramentas genéricas ou histórico sem necessidade.
- Corrigir a orientação obsoleta de patches em `CLAUDE.md` e `docs/runtime-dependency-inventory.md`, além das referências atuais a 32 MB como capacidade efetiva. Preservar descrições históricas claramente identificadas.
- Se houver novo commit no fork, atualizar o pin pelo fluxo suportado de `bootstrap_dependencies.py`. Preservar a veracidade de `generation_manifest.json`: não atribuir uma geração antiga a um commit novo apenas para passar a verificação; regenerar pelo pipeline se necessário.

## 4. Verificação

Executar e registrar resultados novos, sem tratar execuções anteriores como evidência atual:

1. Testes direcionados de memória/layout no fork e de manifesto no principal, verificando a falha esperada antes de cada correção.
2. `python3 -m unittest discover -s tests`.
3. `cmake --build third_party/PS2Recomp/build-runtime`, usando a configuração existente válida; executar a suíte pertinente do fork.
4. `cmake --build build` e `ctest --test-dir build --output-on-failure`.
5. `python3 tools/bootstrap_dependencies.py --check` e `python3 tools/verify_generation_manifest.py`.
6. Recompilar o probe, se necessário, exclusivamente por `python3 tools/run_desktop_probe_build.py --jobs N`, com paralelismo adequado ao host; executar `desktop_runtime_probe_init` no diretório de build correto. Confirmar que o teste realmente existe, evitando falso sucesso com zero testes.
7. `git diff --check`, revisão dos diffs dos dois repositórios e revisão independente final das mudanças efetivas.

O probe headless comprova inicialização, não gameplay nem viewport HD. Falhas ou dependências ausentes devem ser registradas com o comando e erro, nunca apresentadas como sucesso.

## Registro de execução — 2026-10-28

- Fork: commits `0a0e0ce` (reservas HLE/heap/stacks) e `c33899b` (rejeição de ELF nas reservas e fixtures de stacks relativas ao tamanho da RAM). Revisão independente identificou a lacuna de carregamento ELF; regressão reproduzida e corrigida.
- Suíte do fork: **487/487 passaram**, executada a partir da raiz do fork. A configuração anterior desabilitava analyzer/recompiler apesar de habilitar testes; habilitar ambos e reutilizar dependências locais resolveu a ausência de ELFIO. As duas fixtures antigas de stacks ainda usavam endereços do teto de 32 MiB e foram atualizadas; executar da raiz também resolve o teste que lê `instructions.h`.
- Principal: **370 testes Python, 4 skips, sem falhas**; **49/49 testes C++ passaram**. Regressões de overflow em `GuestMemory` e snapshots foram observadas antes das correções.
- Bootstrap: registro direcionado de pin, verificação de HEADs e respeito a pins existentes sem descartar dependências não gerenciadas. Novas regressões reproduziram os problemas antes das correções.
- Pipeline Ghidra e geração foram realmente reexecutados com o novo pin; **58.552 entradas, zero violações, 58.553 unidades C++**. Manifesto e lockfile verificados, sem apenas trocar o SHA da geração anterior.
- Rebuild completo do probe iniciado pelo wrapper com `--jobs 2`; **ainda em execução**. O teste headless da nova compilação permanece pendente; não considerar o binário antigo como validação.
- Decisão de escopo: manter a API de callback de apresentação existente e não ampliar a viewport. Custo: HD continua sendo uma etapa separada; a RAM extra não é uma reserva exclusiva do mapa.

## Critérios de conclusão

- 64 MiB acessíveis pelos caminhos de memória testados, sem alias indevido com a metade inferior.
- Heap, pools HLE e stacks com reservas não sobrepostas e cobertura de limites.
- Bootstrap reproduz o fork sem reaplicar patches; fixtures acompanham o pin sem perder testes negativos.
- Documentação alinhada à implementação; validação nova reportada com limitações explícitas.
- Commits locais apenas das alterações desta tarefa, primeiro no fork e depois no principal quando necessário, sem incluir mudanças alheias nem publicar remotamente.
