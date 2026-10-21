# Multiplayer LAN Implementation Plan

> **Para agentes:** use `superpowers:subagent-driven-development` ou `superpowers:executing-plans`. Este plano só pode ser executado após aprovação do usuário. Não declare o multiplayer funcional sem o teste real descrito na Task 6.

**Goal:** Permitir que instâncias do port recompilado descubram, criem e entrem em uma sessão LAN, preservando a simulação lockstep original e substituindo apenas o transporte de alto nível.

**Architecture:** Primeiro, estabelecer por matching 100% o contrato guest de rede. Em paralelo, construir transporte UDP e descoberta host-side independentes do jogo. Depois, só com contratos e semântica de entrega comprovados, resolver hooks por evidência do ELF/decompilação e integrá-los ao runtime em um grupo atômico, ativado explicitamente.

**Tech Stack:** C++20, sockets do sistema, CMake/CTest, resolvedor Python existente, projeto `aoe` com MWCC/EE-GCC via wibo, runtime PS2Recomp pinado.

**Spec:** `docs/sot/multiplayer-lan-architecture.md` — rascunho não rastreado; contém hipóteses a validar e material a sanear antes de qualquer commit.

## Autorizações e restrições globais

- O usuário escolheu **“Incluir trabalho de matching”**. Isso autoriza somente o matching necessário da camada de rede em `../aoe`; não autoriza alterar outros subsistemas, binários originais, compiladores ou requisitos de matching.
- Preservar `README.md`, `docs/adr/0001-kcet-boundary-replacement.md`, `docs/architecture.md`, `docs/hd-assets.md`, `docs/sot/kcet-stall-log.md`, `docs/plans/`, `docs/sot/multiplayer-lan-architecture.md`, `docs/sot/subsystems-assets-evolution.md` e `ps2_log.txt` como estão no checkout principal. Trabalhar em worktrees isoladas e não commitá-los sem decisão explícita.
- Não editar o SDK sujo de `aoe-recomp` nem duplicar o diagnóstico SIF/RPC. O erro de socket negativo atual é bloqueio de boot separado; não use um valor negativo como handle.
- Nenhum endereço, função, disassembly ou trecho derivado do jogo em arquivos rastreados deste repositório. O resolvedor produz endereços apenas em `game/SLES-50282/kcet_hooks.ini`, ignorado.
- Não alterar C++ gerado, TOML, `skip`, `stub`, políticas de boot ou validação de progresso.
- A solução não promete Internet, cross-play com PC/HD ou mais de dois jogadores. Esses pontos dependem de evidência do jogo.
- Não rodar builds completos duplicados das unidades geradas. A validação real é coordenada com `aoe-recomp`.
- No projeto `aoe`, respeitar `CLAUDE.md` e `docs/testing.md`; comunicação entre agentes deve usar frases curtas.

## Review Focus

- Pacote UDP malformado, truncado ou maior que o limite: rejeitar sem ler fora do buffer e sem alterar a sessão.
- Duplicata, perda e reordenação: aplicar somente as garantias exigidas pelo contrato; sem contrato, a integração fica bloqueada.
- Beacon antigo ou repetido: expirar por relógio monotônico e nunca listar sessões obsoletas.
- Segundo peer, peer desconhecido ou sessão incompatível: recusar de forma explícita, sem corromper estado.
- Falha parcial de resolução de hook: não ativar qualquer parte do grupo e retornar erro legível.

## Task 1: Matching dos contratos guest de rede em `aoe`

**Files:**
- Create/modify somente no projeto `/home/samuelcaldas/source/repos/aoe`, em worktree isolada: `src/network_*.cpp`, `include/network_*.h`, `tests/test_network_*_matching.py`, `tests/test_network_*_behavior.py`, `configure.py`, `config/SLES-50282/symbols.txt` e `docs/` estritamente necessários.
- Evidência bruta: apenas em diretórios ignorados.

**Interfaces:**
- Produces: contrato verificado de ciclo de vida, enumeração/descoberta, criação/entrada, identidade de peers, envio, recepção, filas, callbacks, threads, timeouts e códigos de erro.
- Produces: limite nativo comprovado de jogadores e requisitos de confiabilidade, ordenação, duplicatas e perda.

- [ ] **Step 1: Confirmar disponibilidade e coordenação.** Rodar `ListAgents`; se não houver sessão responsável pela decompilação, informar ao usuário e executar somente em worktree isolada. Registrar `git status` antes e depois.
- [ ] **Step 2: Preparar o ambiente de matching.** Verificar `orig/SLES-50282/AOK.ELF`, `config/SLES-50282/build.sha1` e compiladores: MWCC para C++ do jogo e EE-GCC para C KCET. Se faltarem pré-requisitos, marcar **BLOCKED**.
- [ ] **Step 3: Escrever testes comportamentais que falham.** Cobrir cada função de rede selecionada: layouts, retornos, erros, ordem de chamadas e estado. O teste deve falhar pela ausência do comportamento, não por erro de coleção ou compilador.
- [ ] **Step 4: Implementar uma função/grupo por vez em C++ ou C compatível com o compilador correto.** Rodar o teste comportamental e, depois, compilar apenas o alvo de objeto efetivamente gerado.
- [ ] **Step 5: Verificar 100% byte e relocation.** Comparar o objeto recompilado com os limites completos das funções originais depois de resolver relocations. Não aceitar percentual menor, diferença de relocation ou substituição por `.word` para mascarar falta de matching.
- [ ] **Step 6: Registrar objeto e símbolos somente após match completo.** Rodar regressão relevante do projeto, relatar falhas/skips e commitar no worktree de `aoe`.
- [ ] **Step 7: Emitir relatório sanitizado de contrato.** Entregar a este projeto somente semântica e requisitos, sem endereços ou código. Informar `aoe-recomp` do resultado.

**Gate:** se qualquer função necessária não alcançar 100%, ou se o contrato de entrega/jogadores não for comprovado, as Tasks 4–5 de integração permanecem bloqueadas. As Tasks 2–3 podem continuar como infraestrutura, mas não como multiplayer funcional.

## Task 2: Transporte UDP host-side

**Files:**
- Create: `network/include/aoe2/net/network_transport.hpp`, `network/include/aoe2/net/udp_lan_transport.hpp`, `network/src/udp_lan_transport.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/cpp/net_tests.cpp`

**Interfaces:**
- Produces: `namespace aoe2::net`; `PeerAddress`, `NetPacket`, resultado/erro tipado e `INetworkTransport` com inicialização, shutdown, `SendTo`, `Broadcast` e `PollPacket` não bloqueantes.
- Consumes: apenas APIs de sistema; nenhuma dependência nova.

- [ ] **Step 1: Escrever testes que falham** para inicialização, porta ocupada, dois endpoints em localhost, poll vazio, tamanho máximo e shutdown idempotente.
- [ ] **Step 2: Rodar `ctest -R net_transport` e confirmar falha por ausência da biblioteca.**
- [ ] **Step 3: Implementar `aoe2_net`** como biblioteca separada, com flags privadas e sem propagar socket ao código gerado.
- [ ] **Step 4: Testar apenas em loopback.** Broadcast real não participa dos testes automatizados.
- [ ] **Step 5: Executar suites existentes e commitar** `feat: add LAN datagram transport`.

## Task 3: Descoberta e codec de sessão

**Files:**
- Create: `network/include/aoe2/net/lan_discovery.hpp`, `network/src/lan_discovery.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/cpp/lan_discovery_tests.cpp`

**Interfaces:**
- Produces: `LanSessionAdvertisement`, codec versionado com ID de compatibilidade, limites, UTF-8 válido, expiração por relógio injetável e lista de sessões.
- Consumes: `INetworkTransport` da Task 2.

- [ ] **Step 1: Escrever testes que falham:** roundtrip, versão/ID incompatível, nome inválido, truncamento, tamanho excessivo, duplicata, expiração e relógio falso.
- [ ] **Step 2: Rodar o alvo e confirmar falha por ausência do codec.**
- [ ] **Step 3: Implementar codec e cache** usando valores de protocolo fixados explicitamente no contrato; a porta 23000 é configuração proposta, não fato do jogo.
- [ ] **Step 4: Verificar resultado e commitar** `feat: add LAN session discovery`.

## Task 4: Semântica de sessão, resolvedor e contrato de entrega

**Files:**
- Create: `network/include/aoe2/net/lan_session.hpp`, `network/src/lan_session.cpp`, `tools/kcet_resolver/network.py`
- Modify: `tools/resolve_kcet_hooks.py`
- Test: `tests/cpp/lan_session_tests.cpp`, `tests/test_kcet_network_resolver.py`

**Interfaces:**
- Consumes: relatório sanitizado de contratos da Task 1 e as camadas anteriores.
- Produces: máquina de sessão com identidade, fila limitada, estado de peer, política de entrega derivada do contrato e resolver de grupo de rede em namespace próprio do arquivo local.

- [ ] **Step 1: Escrever testes que falham** de entrega conforme contrato: duplicatas, perda, reordenação, peer desconhecido, sessão cheia, fila cheia e encerramento. Se o contrato exigir confiabilidade, incluir ACK/retransmissão testados com relógio falso antes de qualquer integração.
- [ ] **Step 2: Escrever testes Python que falham** para preservar chaves de mouse, alterar somente o grupo de rede, executar ambos os resolvedores em qualquer ordem, falhar sem escrita parcial e rejeitar alvo ambíguo.
- [ ] **Step 3: Implementar a sessão e o resolvedor.** Gravar de forma atômica e preservar chaves de outros grupos. Resolver por ELF/mapa e cruzar com a decompilação; divergência é erro.
- [ ] **Step 4: Rodar suites C++ e Python completas relevantes e commitar** `feat: add LAN session semantics and hook resolver`.

## Task 5: Hooks e serviço de runtime

**Files:**
- Create: `game_hooks/src/kcet_net_overrides.cpp`, `runtime/include/aoe2/runtime/lan_runtime_session.hpp`, `runtime/src/lan_runtime_session.cpp`
- Modify: `cmake/Aoe2Recompiled.cmake`, `cmake/Aoe2Runtime.cmake`, `platform/common/src/game_main.cpp`, `README.md`, `docs/game-tty.md` quando necessário, `docs/adr/0001-kcet-boundary-replacement.md`
- Test: `tests/cpp/lan_runtime_tests.cpp`, `tests/test_lan_cmake.py`

**Interfaces:**
- Consumes: sessão LAN e arquivo local de hooks.
- Produces: serviço RAII por `PS2Runtime`, associado antes do `loadELF`, com poll limitado e opção explícita de ativação; padrão desativado.

- [ ] **Step 1: Escrever testes que falham** para isolamento entre runtimes, teardown em todos os caminhos, handlers sem chamada guest em thread host, filas limitadas, recusa de ativação parcial e erro quando falta qualquer alvo.
- [ ] **Step 2: Implementar validação prévia completa** (`hasFunction`, proveniência e disjunção com mouse) antes de chamar `replaceFunction`. Nenhum grupo parcial pode ficar ativo.
- [ ] **Step 3: Integrar serviço e hooks** usando o scheduler EE e o padrão do TTY. Sockets só abrem quando o usuário solicita LAN.
- [ ] **Step 4: Manter LAN OFF por padrão e documentar interface, porta e nome de sessão por configuração explícita, sem credenciais.**
- [ ] **Step 5: Rodar testes runtime-only em Release/Debug, ON/OFF e commitar** `feat: integrate LAN runtime hooks`.

## Task 6: Gate real com `aoe-recomp`

**Files:**
- Create: `docs/lan-validation.md` e evidência ignorada em `build/lan-validation/`

- [ ] **Step 1: Notificar `aoe-recomp`** com commit, configuração, resumo de contratos, testes executados e limitações; combinar quem compila e quais displays/portas usam.
- [ ] **Step 2: Entregar roteiro de duas instâncias:** boot, abrir lobby, descobrir/criar, entrar, iniciar partida, enviar comandos e comparar sincronização/determinismo.
- [ ] **Step 3: Aguardar o resultado real** de `aoe-recomp`; não iniciar builds duplicados do jogo enquanto aguardam.
- [ ] **Step 4: Classificar separadamente boot e LAN.** Se o jogo parar antes do lobby, registrar bloqueio de boot e não declarar sucesso LAN.
- [ ] **Step 5: Atualizar documentação** com evidência, limites e logs, sem endereços ou código do jogo.

## Verificação mínima

```bash
python3 -m unittest discover -s tests
cmake -S . -B build/lan-unit -G Ninja
cmake --build build/lan-unit -j 3
ctest --test-dir build/lan-unit --output-on-failure
git diff --check
```

No projeto `aoe`, usar exatamente os comandos de `docs/testing.md` e do toolchain para teste nativo e comparação de objeto. Testes runtime-only devem usar o contêiner `docker-dev` com limites de CPU/memória e não compilar as 58 mil unidades geradas.

## Self-review

- A autorização de matching e a coordenação com `aoe-recomp` estão refletidas.
- UDP bruto não é considerado confiável nem equivalente ao DirectPlay sem prova.
- Nenhum valor de protocolo do jogo é inventado; a porta 23000 permanece proposta.
- Hooks só são liberados após matching 100%, resolver comprovado e ativação atômica.
- O sucesso final depende de teste real de duas instâncias, não apenas de testes de socket.
