# Módulos nativos `kt_mouse` e `kt_keybd` (plano 3 de 8 da camada KCET nativa) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Substituir a lógica dos módulos `kt_mouse` e `kt_keybd` (filas circulares, portas, eventos, atualização por quadro) por código nativo sobre a memória guest, com o mouse e o teclado do host alimentando o jogo, cada módulo atrás do seu interruptor.

**Architecture:** O nível de dispositivo já existe: o `kcet_hooks.ini` substitui por modelos do host as funções de RPC dos dois módulos (`kcet.mouse.*`, `kcet.keyboard.*`), e isso cobre as 6 funções do `kt_usbkeybd_drv` e 4 das 45 do `kt_mouse`. Este plano porta o resto (41 funções do mouse, 42 do teclado) como modelos C++ sobre uma interface de acesso à memória guest, conversando com os mesmos serviços de RPC por chamada direta, não por despacho guest. A prova de fidelidade é o comparador de snapshots do pad: o instrumento e a linha de base determinística vêm antes do código portado.

**Tech Stack:** C++20, CMake/Ninja, ctest, Python `unittest`, raylib 5.5 (já usado), `EeScheduler` e hooks do PS2Recomp pinados.

**Spec:** `docs/superpowers/specs/2026-10-24-kcet-native-layer-design.md` (módulo 3 da ordem aprovada). Padrões: `docs/superpowers/plans/2026-10-24-native-pad-module.md`, `docs/sot/pad-libpad-semantics.md`.

## Global Constraints

- Fonte: `~/repos/aoe/src/kcet/{kt_mouse,kt_keybd,kt_usbkeybd_drv}.c` e `include/kt_{mouse,keybd,usbkeybd_drv}.h` (somente leitura). O decomp **não é fonte confiável de fluxo de controle neste projeto**: no `kt_libpad` ele divergiu do binário em mais de dez funções. Os sidecars (`docs/recomp/*.yaml`) dizem `confidence: high` por metadado, sem verificação contra o binário. Os testes "matching" do decomp usam palavras de instrução do original para resolver alguns alvos de salto, então não são prova independente. Uma sessão de decomp (`aoe-decomp`) informou isso em 2026-10-24.
- **O binário é a autoridade.** O código gerado (somente leitura, `generated/SLES-50282/`) mostra o que o original faz; nunca copie código gerado para arquivo versionado. Diferença entre decomp e binário: porte o binário, fixe em teste, registre em `docs/sot/` para o agente do decomp.
- O comparador de snapshots do original contra o nativo é a prova. Teste de unidade passando não prova fidelidade; testes ignorados por falta do decomp não contam como aprovados; `confidence: high` de sidecar não conta.
- Sem stubs e sem atrasos ou estados inventados para fazer o teste passar. Não mascare bytes divergentes do snapshot.
- Interruptores `mouse` e `keyboard` já existem em `kNativeModuleNames`; ficam desligados por padrão. O módulo só instala com o seu interruptor ligado; a camada de dispositivo (`kcet_hooks.ini`) continua independente.
- Binários, disassembly, exportações do Ghidra, mapas de função e C++ gerado nunca entram no git. Endereços e nomes do decomp podem. Não editar `third_party/`, código gerado nem o decomp.
- Um commit por tarefa, terminando com `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`. Os commits são esperados nesta execução.
- Build do jogo: `python3 tools/docker_build.py --platform desktop --diagnostics --jobs 4`; após **toda** alteração de fonte instrumentada, reconstrua antes de medir (já houve log gerado por binário antigo). Testes do host: `cmake -B build -G Ninja && cmake --build build && ctest --test-dir build --output-on-failure`.
- Contrato de agendamento (aprendido no kernel): nada que possa bloquear roda dentro de um modelo. Funções que esperam semáforo ou vblank ficam geradas (adiadas).
- Estilo do repositório: funções curtas, sem funções de uma linha densas, comentários só para o porquê; origem (repo e commit do decomp) no cabeçalho dos arquivos portados.

## Process (lições aplicadas)

- Agentes de implementação desistem cedo em portas grandes e já produziram APIs inventadas: dê-lhes lotes pequenos (uma camada de dependência) e verifique cada afirmação deles contra os arquivos e os logs. Revisores precisam de um comando e de um critério objetivo (snapshot, teste), não de uma opinião.
- Quando a porta de uma camada não bater, o primeiro byte divergente do snapshot dirige o próximo reparo; um achado por vez, com regressão que reproduz exatamente os bytes.

## Review Focus

1. Fila circular de eventos do mouse: escrita, leitura e envolvimento do índice com 0, 1, N e N+1 eventos; evento perdido ou duplicado quando a fila enche. (Tasks 4 e 6)
2. Botões e deltas: o mesmo movimento do host aparece uma única vez no jogo, sem acumular em ciclos de leitura vazios, com sinal e saturação em 8 bits. (Tasks 4 e 6)
3. Teclado: tecla pressionada e solta dentro do mesmo quadro não se perde; repetição, LED e tipo de caps lock seguem o original. (Tasks 5 e 6)
4. Porta ou índice de mouse fora da faixa (`n_mouse` 0, 2 ou negativo; porta livre inexistente) falha com a asserção do original, sem escrever fora da área de trabalho. (Tasks 4 e 5)
5. Interruptor desligado não muda nada; ligado sem a camada de dispositivo instalada falha com mensagem clara. (Task 7)

---

### Task 1: Acesso compartilhado à memória guest

**Files:**
- Create: `runtime/include/aoe2/runtime/guest_access.hpp`, `game_hooks/src/guest_rdram_access.hpp`
- Modify: `pad/include/aoe2/pad/pad_guest_memory.hpp` (alias da interface compartilhada), `game_hooks/src/kcet_libpad.cpp` (usa o adaptador compartilhado), `tests/cpp/fake_pad_guest_memory.hpp`

**Interfaces:**
- Produces: `aoe2::runtime::GuestAccess` (`Load8/16/32`, `Store8/16/32`, `ReadBytes`, `WriteBytes`, `ReportAssertion(file,line,expression)`), exatamente os métodos de `PadGuestMemory`; `using PadGuestMemory = aoe2::runtime::GuestAccess;`. `aoe2::game_hooks::RdramGuestAccess final : GuestAccess` com `Bind(uint8_t* rdram)` e a verificação de faixa inteira do `RdramPadMemory` atual (movida, sem mudar comportamento).

- [ ] **Step 1: Write the failing test** `tests/cpp/guest_access_tests.cpp`: um `GuestAccess` falso é aceito onde `PadGuestMemory` era usado (compilação do alias) e `native_pad_memory_tests` continua passando com o adaptador movido.
- [ ] **Step 2: Run** `cmake --build build --target aoe2_guest_access_tests` — Expected: falha (cabeçalho inexistente).
- [ ] **Step 3: Implement** a interface e o adaptador; registrar o teste no CMake.
- [ ] **Step 4: Run** `ctest --test-dir build --output-on-failure` — Expected: todos passam, sem mudar a contagem dos testes do pad.
- [ ] **Step 5: Commit** `refactor: share the guest memory access interface`.

---

### Task 2: Entradas sintéticas determinísticas e observador de snapshots genérico

**Files:**
- Modify: `game_hooks/src/pad_snapshot.hpp` (renomear conceitualmente para observador por regiões), `game_hooks/src/kcet_mouse_overrides.cpp`, `game_hooks/src/kcet_keyboard.cpp`, `game_hooks/src/aoe2_call_trace.cpp`
- Create: `game_hooks/src/input_synthetic.hpp`, `game_hooks/src/state_snapshot.hpp`, `tests/cpp/input_synthetic_tests.cpp`, `tests/cpp/state_snapshot_tests.cpp`

**Interfaces:**
- Produces: com `AOE2_INPUT_SYNTHETIC=1`, o mouse do host é substituído por um gerador dirigido pela contagem de leituras (não por tempo): a cada 50 leituras um deslocamento fixo `(+3, -2)` e o botão esquerdo alterna a cada 200; o teclado emite a tecla `A` pressionada por 30 leituras a cada 120. Constantes e padrão em `input_synthetic.hpp`, com testes de que a sequência é a mesma em duas instâncias. `StateSnapshot::Log(label, ordinal, regions)` registra `[state-snapshot] label=<mouse|keyboard> n=<k> fnv=<hash> bytes=<hex>` para uma lista de regiões `{endereço, tamanho}` na amostra 1..16 e depois a cada 64ª, com checagem de faixa e marcador `[state-snapshot-skip]` explícito; sem a variável `AOE2_STATE_SNAPSHOT=1` imprime apenas `[state-snapshot-config] enabled=0`.
- Regiões (das externas dos módulos no decomp): mouse `0x509F00..0x50A300`, teclado `0x50A2C0..0x50A540`, cada uma com o tamanho exato conferido em `include/kt_mouse.h`, `include/kt_keybd.h` e no código gerado.
- O ponto de captura de cada módulo é o retorno da função de atualização por quadro (`ktMouseProcessEvents` e a equivalente do teclado), tanto no original (via o trace) quanto no nativo (no handler), para que as duas medições observem o mesmo ponto lógico.

- [ ] **Step 1: Write the failing tests** para a sequência sintética (mesma saída em duas instâncias, deslocamentos e botões nos índices acima) e para o observador (região fora da faixa gera `skip`, não leitura; desabilitado não lê memória).
- [ ] **Step 2: Run** os alvos — Expected: falha de compilação.
- [ ] **Step 3: Implement** o gerador e o observador; ligar o gerador ao caminho de leitura dos dois serviços de RPC já existentes (`MouseRead`, `KeyboardRead`) sem mudar o caminho real do host.
- [ ] **Step 4: Run** `ctest` e `python3 -m unittest discover -s tests` — Expected: passam.
- [ ] **Step 5: Reconstruir** o jogo e rodar **duas vezes** o original (sem módulos nativos) por 30 s com `AOE2_INPUT_SYNTHETIC=1 AOE2_STATE_SNAPSHOT=1`: os snapshots das duas execuções precisam ser byte a byte iguais. Se não forem, a linha de base é ruidosa; descubra e remova a fonte do ruído antes de seguir. Registrar contagens em `docs/sot/input-modules-semantics.md`.
- [ ] **Step 6: Commit** `feat: deterministic synthetic input and region snapshot observer`.

---

### Task 3: Cobertura do nível de dispositivo e mapa de funções

**Files:**
- Create: `tests/test_input_modules_coverage.py`, `docs/sot/input-modules-semantics.md`

**Interfaces:**
- Produces: um teste Python que, a partir dos `.c` do decomp (ignorado se ausente) e de um arquivo de dados `game_hooks/src/input_module_entries.txt`, verifica: as 6 funções do `kt_usbkeybd_drv` são exatamente os 6 papéis `kcet.keyboard.*`; as 4 do `kt_mouse` (`bind`, `get_info`, `read`, `device_info`) são os papéis `kcet.mouse.*`; as demais 41 + 42 funções estão ou em `ENTRY` ou em `DEFERRED` com motivo, sem sobreposição com o nível de dispositivo.
- O documento registra, por função e **verificado contra o binário**, o conjunto adiado. Candidatos já visíveis: as que esperam semáforo (`func_00134F90`/`func_00134FB8` e as equivalentes do teclado), as que criam ou apagam semáforo, as que esperam vblank (`func_00135BE8`, `func_00135D60`) e as que chamam o kernel (`func_001348A8`).

- [ ] **Step 1: Write the failing test** com as contagens 6/6, 4/45, 41 e 42 (mais os conjuntos adiados, vazios até a Task 4/5 preenchê-los) e a regra de não sobreposição.
- [ ] **Step 2: Run** `python3 -m unittest tests.test_input_modules_coverage` — Expected: falha.
- [ ] **Step 3: Implement** o arquivo de dados com as funções classificadas, lendo o código gerado de cada função suspeita de bloquear para decidir o adiamento (nada de palpite).
- [ ] **Step 4: Run** o teste — Expected: passa. Contagens exatas assertadas mesmo sem o decomp; só a comparação com o fonte é ignorável.
- [ ] **Step 5: Commit** `test: coverage map of the mouse and keyboard module functions`.

---

### Task 4: Modelo `kt_mouse` sobre memória guest

**Files:**
- Create: `mouse/include/aoe2/mouse/kt_mouse_model.hpp`, `mouse/src/kt_mouse_leaf.cpp`, `mouse/src/kt_mouse_mid.cpp`, `mouse/src/kt_mouse_top.cpp`, `tests/cpp/kt_mouse_model_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `GuestAccess` (Task 1), um serviço `MouseRpc` (abaixo) e `Check`-style testes com `FakeGuestMemory` (reuso do fake do pad).
- Produces: `aoe2::mouse::MouseRpc` (interface pura: `Info(uint32_t reply)`, `Read(int port, uint32_t reply)`, `DeviceInfo(int port, uint32_t reply)` escrevendo os 32 bytes de resposta) e `class KtMouseModel(GuestAccess&, MouseRpc&)` com um método por função portada (nome pelo papel, comentário com o símbolo do decomp), em camadas de dependência: folhas (índices e acessores do anel de leitura e escrita, `func_00134E50` init, `func_00135008`, `func_00135058`, `func_00135090`, `func_001350E0`, `func_001351B0`, `func_001351B8`, `func_00135238`), camada média (`func_00135118`, `func_001352D8`, `func_001353C0`, `func_00135530`, `func_001356E8`, `func_00135798`) e topo (`func_00135870` posta evento, `func_00135BF0` processa eventos). O processamento de eventos chama o `MouseRpc` onde o original chama os RPCs (`func_00135DE8`, `func_00135E30`, `func_00135E78`).
- Layout das estruturas (`KT_MOUSE` de 0xB0 bytes, `KT_MOUSE_WORK`, blocos de 0xC) **conferido no código gerado**, não no cabeçalho do decomp.

- [x] **Step 1: Write the failing tests** `kt_mouse_model_tests.cpp`, por camada: init com `n_mouse` 0, 1 e 2 aceitos e 3 causando a asserção (conferido no binário); escrita e leitura no anel com 0/1/N/N+1 eventos e envolvimento do índice; `PostEvent` seguido de `ReadPacket` devolve botões, `dx`, `dy` e roda conforme o original; `ProcessEvents` com um `MouseRpc` falso devolvendo eventos de 4 bytes grava eventos no anel e repete até esvaziar; porta inexistente e fila cheia reproduzidas por teste. Os valores esperados vêm do código gerado, não do decomp.
- [x] **Step 2: Run** `cmake --build build --target aoe2_kt_mouse_model_tests` — falhou pela ausência do cabeçalho; depois, pelos métodos ainda não implementados.
- [x] **Step 3: Implement** camadas folha, média e topo; 32 funções ENTRY mapeadas, com revisão independente do fluxo de controle. Os dois retornos de alinhamento foram conferidos diretamente no ELF.
- [x] **Step 4: Run** `ctest --test-dir build --output-on-failure` — 18/18; Python 308 testes, dois skips. Revisão independente sem achados acionáveis. Sem medição de paridade nesta task.
- [x] **Step 5: Commit** `feat: native kt_mouse model over guest memory`.

---

### Task 5: Modelo `kt_keybd` sobre memória guest

**Files:**
- Create: `keyboard/include/aoe2/keyboard/kt_keybd_model.hpp`, `keyboard/src/kt_keybd_leaf.cpp`, `keyboard/src/kt_keybd_mid.cpp`, `keyboard/src/kt_keybd_top.cpp`, `tests/cpp/kt_keybd_model_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `GuestAccess`, `KeyboardRpc` (interface pura análoga: `Info`, `Read`, `DeviceInfo`, `SetLed(mask)`, `SetCapsType(type)`).
- Produces: `class KtKeybdModel(GuestAccess&, KeyboardRpc&)`, mesma organização em camadas (folhas: alocação e estado da fila, payload e preenchimento, requisição de LED e repetição; topo: atualização por quadro). O teclado sintético do host chega pelo `KeyboardRpc::Read` existente.

- [x] **Step 1: Write the failing tests**: init/alocação, filas vazias/cheias/envolvimento, payload e preenchimento, repetição e press/release no mesmo quadro, requisições e asserções. Testes de modelo e processamento separados para manter arquivos abaixo de 200 linhas.
- [x] **Step 2: Run** `cmake --build build --target aoe2_kt_keybd_model_tests` — falhou pelo cabeçalho ausente e depois pelos métodos ainda não implementados.
- [x] **Step 3: Implement** folhas, camada média e topo; 35 ENTRY mapeadas. Revisão binária não confirmou a suspeita anterior em `func_00136850`. `KeyboardRpc::SetLed` e `SetCapsType` recebem índice e valor como os wrappers originais.
- [x] **Step 4: Run** `ctest` e Python — 19/19 CTests; Python 308 testes, dois skips. A regressão do limite de 14 bytes foi verificada por mutação RED→GREEN. Sem paridade de runtime reivindicada.
- [x] **Step 5: Commit** `feat: native kt_keybd model over guest memory`.

---

### Task 6: Adaptadores, serviços de RPC compartilhados e interruptores

**Files:**
- Create: `game_hooks/src/kt_mouse_entries.inc`, `game_hooks/src/kt_keybd_entries.inc`, `game_hooks/src/kcet_mouse_module.cpp`, `game_hooks/src/kcet_keyboard_module.cpp`, `game_hooks/include/aoe2/game_hooks/native_input.hpp`
- Modify: `game_hooks/src/kcet_mouse_overrides.cpp`, `game_hooks/src/kcet_keyboard.cpp` (expõem os serviços de RPC existentes como funções sobre `GuestAccess`, mesmo comportamento), `cmake/Aoe2Recompiled.cmake`

**Interfaces:**
- Produces: `aoe2::mouse::MouseRpc& EnsureMouseRpc()` e `aoe2::keyboard::KeyboardRpc& EnsureKeyboardRpc()` (singletons compartilhados entre o gancho de dispositivo e o módulo, idempotentes, sem depender da ordem de registro estático); os `.inc` no formato do `kt_libpad_entries.inc` (`ENTRY_R`/`ENTRY_V`/`DEFERRED`); instaladores que exigem o interruptor do módulo e a camada de dispositivo presentes, e registram `[native-mouse]`/`[native-keyboard]` nas 3 primeiras chamadas e em potências de 2. Argumentos além do quarto vêm de `t0..t3`; retorno em `v0`; `pc = ra`; nada bloqueia. Funções adiadas continuam geradas.
- Os 4 + 6 endereços do nível de dispositivo **não** entram nos `.inc` (já substituídos pelos ganchos).

- [x] **Step 1: Write the failing tests**: `tests/test_native_input_entries.py` (contagens 41 e 42 mais os adiados e a regra de não sobreposição com `kcet_hooks.ini`'s roles) e um teste C++ do mapeamento de argumentos com `FakeGuestMemory`.
- [x] **Step 2: Run** os testes — falha inicial verificada para ambos.
- [x] **Step 3: Implement** os serviços compartilhados e os instaladores (use `hasFunction`, não `lookupFunction`; mensagem clara se o interruptor do módulo estiver ligado sem a camada de dispositivo).
- [x] **Step 4: Run** `ctest`, Python e a checagem de sintaxe de unidade única dos adaptadores novos — 20/20 CTests passaram; Python 310 testes passaram (2 skips); compilação direta via g++ passou.
- [x] **Step 5: Commit** `feat: native mouse and keyboard module adapters behind their switches`.

---

### Task 7: Comparação original contra nativo, até a paridade observada

**Files:**
- Modify: `docs/sot/input-modules-semantics.md`, `docs/sot/kcet-stall-log.md`, design e plano (estado dos módulos)

- [ ] **Step 1: Reconstruir** o jogo e capturar o original (30 s, `AOE2_INPUT_SYNTHETIC=1 AOE2_STATE_SNAPSHOT=1`, `mouse` e `keyboard` desligados); capturar o nativo (`mouse=on`, `keyboard=on` com a camada de dispositivo ativa). Preservar e restaurar `native_modules.ini` e `trace_addrs.txt` exatos.
- [ ] **Step 2: Comparar** todos os pares `(rótulo, n)` byte a byte nas regiões completas, relatar o primeiro byte divergente e o contador de amostras de entrada.
- [ ] **Step 3: Para cada divergência:** reproduzir em teste os bytes exatos, ler o código gerado da função responsável, corrigir o mínimo, reconstruir e repetir. Nunca mascarar bytes nem alterar a entrada sintética para alinhar contadores.
- [ ] **Step 4: Ao fechar:** registrar a paridade observada (pares comparados, iguais), os limites (cenário sintético, só o que o boot exercita; entrada real e arrastar com o mouse só depois do boot destravar) e o estado de cada função (portada e coberta por snapshot; portada sem cobertura; adiada com motivo).
- [ ] **Step 5: Commit** `docs: record native mouse and keyboard observed parity`.

---

## Em aberto

- **Travamento de boot** (`stalled_in_loop`): enquanto o jogo não passa do laço atual, a comparação só cobre o que o boot e o idle exercitam (22 de 45 funções do mouse, 24 de 42 do teclado no trace de 90 s). Funções que só rodam em jogo ficam portadas sem cobertura de snapshot; isso precisa ficar explícito no documento, não implícito.
- Acesso direto do motor às áreas de trabalho (estruturas apontadas por `func_00135530` e `func_00136690`): confirmar no código gerado se o motor lê campos sem chamar funções; se ler, o layout é contrato público e entra no teste de snapshot.
- Mouse por toque (Switch portátil) e o teclado virtual: fora desta entrega; a interface `MouseRpc` já os comporta.
- Teste diferencial por função (original contra nativo sobre memória sintética) continua adiado; o snapshot de regiões é a prova desta entrega.

## Self-review

- **Cobertura do design:** módulo 3 da ordem aprovada; interruptor por módulo; instrumento e linha de base antes da porta; binário como autoridade; sem stubs.
- **Consistência:** `GuestAccess`, `MouseRpc`, `KeyboardRpc`, `KtMouseModel`, `KtKeybdModel` e os `.inc` aparecem com a mesma forma entre as tarefas.
- **Risco declarado:** a cobertura da prova é limitada ao que o boot exercita enquanto o travamento existir; o plano diz isso em vez de prometer paridade total.
