# Módulo nativo `pad` e vibração (plano 2 de 8 da camada KCET nativa) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Dar ao jogo um gamepad e vibração do host substituindo, nesta ordem, a libpad da Sony (sem IOP `padman` aqui) e o módulo `kt_libpad` do decomp, cada um atrás de um interruptor.

**Architecture:** Duas camadas. (1) Um `PadDevice` puro (sem `R5900Context`) implementa os serviços de libpad que o `kt_libpad` usa e produz o relatório de 32 bytes no formato da libpad pública; um backend do host (raylib no desktop) fornece estado do controle e recebe a vibração. Handlers nativos colocados nos endereços Sony chamam o `PadDevice`; interruptor `pad_device`. (2) Depois, o `kt_libpad` é portado para C++ sobre a memória guest (a estrutura `PadSlot` de 512 bytes continua na memória guest), falando com o mesmo `PadDevice`; interruptor `pad`. O padrão do adaptador e a regra de reagendamento são os do plano do kernel.

**Tech Stack:** C++20, CMake/Ninja, ctest, Python `unittest`, raylib 5.5 (já usado pelos hooks de mouse e teclado), `EeScheduler` do PS2Recomp pinado.

**Spec:** `docs/superpowers/specs/2026-10-24-kcet-native-layer-design.md` (ordem aprovada, módulo 2). Padrões herdados: `docs/superpowers/plans/2026-10-24-native-kernel-module.md`.

## Global Constraints

- Fonte de verdade: `~/repos/aoe/src/kcet/kt_libpad.c` e `include/kt_libpad.h` (somente leitura; o decomp não pode ser modificado a partir daqui). Cada arquivo que porta lógica dele leva a origem (repositório `samuelcaldas/age-of-empires-2-decomp`, commit via `git -C ~/repos/aoe rev-parse HEAD`, e "working tree" se `kt_libpad.c`/`kt_libpad.h` tiverem mudanças locais).
- **Regra de fidelidade:** o nativo reproduz o decomp, inclusive esquisitices que parecem bug (retornos fixos em 0, flags ligadas antes de validar, comparações estranhas). Replique e fixe em teste; nunca corrija. Só valem diferenças de representação no host e texto de log. **Exceção, descoberta na Task 1:** quando o código gerado do binário (somente leitura, em `generated/`) contradiz o decomp, o binário é a autoridade; porte o comportamento do binário, fixe em teste e registre a divergência em `docs/sot/pad-libpad-semantics.md` para o dono do decomp. Funções com muitos desvios (despachante por estado, máquina de estados) devem ser conferidas contra o código gerado.
- Sem stubs e sem retorno inventado fora do que o decomp ou a libpad pública definem; o que for desconhecido vai para "Em aberto" e falha explícita com log.
- Interruptores desligados por padrão; arquivo `native_modules.ini` ao lado do ELF. Nomes novos em `kNativeModuleNames`: `pad_device`; `pad` já existe.
- Binários, disassembly, exportações do Ghidra, mapas de função e C++ gerado nunca entram no git. Endereços e nomes do decomp podem (autorização do dono de 2026-10-24).
- Não editar `third_party/`, código gerado nem o decomp. Um commit por tarefa, terminando com `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`.
- Build do jogo por `python3 tools/docker_build.py --platform desktop --diagnostics --jobs 4`; testes do host por `cmake -B build -G Ninja && cmake --build build && ctest --test-dir build --output-on-failure`; validação por `python3 tools/validate_desktop.py --run-seconds 90 --interval 30`.
- **Contrato de agendamento (aprendido no kernel):** qualquer chamada que possa bloquear lança `EeDispatcherTransfer` e descarta os quadros C++; portanto, defina `ctx->pc = ra` antes e deixe o bloqueio como última ação do handler. O `kt_libpad` portado roda atômico dentro do handler, então o semáforo de exclusão mútua do original (`func_00125AA8`/`func_00125AC8`) não é adquirido no nativo (documente isso no cabeçalho do arquivo).
- Estilo: 4 espaços, LF, namespaces `aoe2::pad`, cabeçalhos em `pad/include/aoe2/pad/`, fontes em `pad/src/`.

## Review Focus

1. `Read` devolve o número de bytes pelo modo (`0x41` → 4, `0x73` → 8, `0x79` → 20); nunca 32, porque o `kt_libpad` rejeita retornos fora de 1..31. (Task 4)
2. Botões são ativo-baixo: apertar zera o bit; controle desconectado devolve estado "desconectado" e relatório com tudo solto, sem botão preso. (Tasks 3 e 4)
3. Sticks: -1.0 → 0, 0.0 → 128, +1.0 → 255, valores fora de [-1, 1] são limitados, NaN vira 128. (Task 3)
4. Vibração: valor repetido não chama o host de novo; zeros param o motor; porta desconectada não vibra; o motor grande usa 0..255 e o pequeno é liga/desliga. (Tasks 4 e 5)
5. Porta ou slot fora da faixa (`PortOpen(2, 0)`, `PortOpen(0, 1)`, `SlotMax(2)`) falham com o código de erro, sem travar nem escrever fora do relatório. (Task 4)

---

### Task 1: Evidência dos retornos da libpad no original e interruptor `pad_device`

**Files:**
- Modify: `game_hooks/src/aoe2_call_trace.cpp` (registrar também `v0` na volta, só no build de diagnóstico)
- Modify: `game_hooks/include/aoe2/game_hooks/native_module_switches.hpp`, `game_hooks/src/native_module_switches.cpp`, `tests/cpp/native_module_switches_tests.cpp` (nome `pad_device`)
- Create: `docs/sot/pad-libpad-semantics.md` (tabela dos retornos observados e decisões de semântica; sem trechos do binário)

**Interfaces:**
- Produces: `kNativeModuleNames` inclui `"pad_device"`; `Enabled("pad_device")` funciona como os demais. O documento `pad-libpad-semantics.md` fixa os valores que as Tasks 4 e 6 usam.

- [ ] **Step 1: Write the failing test** em `native_module_switches_tests.cpp`: `"pad_device=on\n"` liga `pad_device` e não liga `pad`; `Enabled("pad_device")` hoje lança `std::invalid_argument`.
- [ ] **Step 2: Run** `cmake --build build --target aoe2_native_module_switches_tests && ctest --test-dir build -R native_module_switches` — Expected: FAIL.
- [ ] **Step 3: Implement** o nome novo na lista.
- [ ] **Step 4: Run** o mesmo comando — Expected: PASS.
- [ ] **Step 5: Implement** no trace: depois de chamar o original, logar `[call-trace] ret addr=0x.. v0=0x..` com a mesma política de contagem; manter o log de entrada.
- [ ] **Step 6: Run** o baseline de 90 s com `/tmp/trace_addrs.pad.txt` copiado para `game/SLES-50282/trace_addrs.txt` (119 endereços do `kt_libpad` mais as externas Sony) e registrar em `pad-libpad-semantics.md`: os valores devolvidos por cada chamada Sony nas primeiras e nas últimas chamadas (init, slot máximo, abrir porta, estado, estado de requisição), a sequência de `p->state` observada, e qual caminho do despachante por estado (`func_001275A0`) o jogo percorre. Expected: tabela preenchida com valores reais; se o estado devolvido no original não permitir inferir o que um controle funcionando devolveria, escrever isso explicitamente e fixar a decisão para a Task 4 (ver "Em aberto").
- [ ] **Step 7: Commit** `feat: pad_device switch and libpad return-value evidence`.

---

### Task 2: Modelo do relatório de 32 bytes (puro)

**Files:**
- Create: `pad/include/aoe2/pad/pad_report.hpp`, `pad/src/pad_report.cpp`, `tests/cpp/pad_report_tests.cpp`
- Modify: `CMakeLists.txt` (`pad/include` nas includes de `aoe2_interfaces`, fonte, executável `aoe2_pad_report_tests`, `add_test(NAME pad_report ...)`)

**Interfaces:**
- Produces:
  - `enum PadButton : uint16_t` com os bits da libpad (ativo-baixo no relatório): `kSelect=0x0001, kL3=0x0002, kR3=0x0004, kStart=0x0008, kUp=0x0010, kRight=0x0020, kDown=0x0040, kLeft=0x0080, kL2=0x0100, kR2=0x0200, kL1=0x0400, kR1=0x0800, kTriangle=0x1000, kCircle=0x2000, kCross=0x4000, kSquare=0x8000`.
  - `struct HostPadState { bool connected; uint16_t buttons; float right_x, right_y, left_x, left_y; }` (`buttons` com 1 = apertado, bits acima).
  - `enum class PadMode : uint8_t { kDigital = 0x41, kAnalog = 0x73, kPressure = 0x79 }`.
  - `struct PadReport { std::array<uint8_t, 32> bytes; int length; }` e `PadReport BuildReport(const HostPadState& state, PadMode mode)`: byte 0 = 0; byte 1 = modo; bytes 2-3 = botões ativo-baixo (byte 2 = low, byte 3 = high); bytes 4-7 = direita X, direita Y, esquerda X, esquerda Y; bytes 8-19 = pressão na ordem Right, Left, Up, Down, Triangle, Circle, Cross, Square, L1, L2, R1, R2 (0xFF apertado, 0 solto, só no modo `kPressure`); restante zero; `length` = 4, 8 ou 20 pelo modo. Desconectado: `length = 0`, tudo zero.
  - `uint8_t AxisToByte(float value)`.

- [ ] **Step 1: Write the failing tests** `pad_report_tests.cpp`: `AxisToByte(-1.0f)==0`, `(0.0f)==128`, `(1.0f)==255`, `(2.0f)==255`, `(-3.0f)==0`, `(NaN)==128`; relatório digital sem botões = bytes 2-3 `0xFF 0xFF`, comprimento 4, byte 1 `0x41`; `kCross` apertado zera só o bit `0x4000` (byte 3 = `0xBF`); `kSelect` zera o bit `0x01` do byte 2; analógico põe sticks nos bytes 4-7 na ordem acima; `kPressure` com `kTriangle` e `kUp` apertados põe `0xFF` exatamente nos índices 10 (Up) e 12 (Triangle), zero nos outros índices de pressão, e `length == 20`; nos modos digital e analógico os bytes 8-19 ficam zero; desconectado → `length == 0` e todos os bytes zero; nenhum byte além do comprimento declarado é diferente de zero nos modos digital e analógico.
- [ ] **Step 2: Run** `cmake --build build --target aoe2_pad_report_tests` — Expected: falha de compilação.
- [ ] **Step 3: Implement** o modelo (arredondamento de `AxisToByte`: `lround((v+1)*127.5)`, que dá 0, 128 e 255 nos três pontos; use esse resultado e confirme os três nos testes).
- [ ] **Step 4: Run** `ctest --test-dir build -R pad_report --output-on-failure` — Expected: PASS.
- [ ] **Step 5: Commit** `feat: libpad 32-byte report model`.

---

### Task 3: `PadDevice` (serviços da libpad, puro) e backend abstrato

**Files:**
- Create: `pad/include/aoe2/pad/pad_device.hpp`, `pad/src/pad_device.cpp`, `tests/cpp/pad_device_tests.cpp`, `tests/cpp/fake_pad_backend.hpp`
- Modify: `CMakeLists.txt` (fonte, executável, `add_test(NAME pad_device ...)`)

**Interfaces:**
- Consumes: `HostPadState`, `PadMode`, `BuildReport` (Task 2).
- Produces: `class PadBackend` (interface pura): `virtual HostPadState Sample(int port) = 0;` `virtual void Rumble(int port, uint8_t small_motor, uint8_t large_motor) = 0;`. E `class PadDevice` com construtor `explicit PadDevice(PadBackend&)` e, devolvendo os códigos da libpad pública (sucesso = 1 onde a libpad devolve 1):
  - `int Init(int mode)` → 1; `int Start(int arg)` → semântica desconhecida (ver Task 1): devolve o valor fixado em `pad-libpad-semantics.md`.
  - `int SlotMax(int port)` → 1 para as portas 0 e 1, 0 caso contrário; `int PortMax()` → 2.
  - `int PortOpen(int port, int slot, uint32_t dma_buffer)` → 1 em porta/slot válidos, 0 caso contrário; `int PortClose(int port, int slot)` → 1 se estava aberta.
  - `int State(int port, int slot)` → estado conforme a Task 1 (0 desconectado, 6 estável, 5 executando comando por uma consulta após cada comando, conforme a evidência); `int ReqState(int port, int slot)` → 0 completo (comandos terminam na hora), nunca 2.
  - `int Read(int port, int slot, std::array<uint8_t, 32>& out)` → `PadReport::length` do modo corrente (0 se a porta não está aberta ou o controle está desconectado), copiando o relatório.
  - `int InfoAct(int port, int slot, int actuator, int term)`: `actuator == -1` com `term == 0` → 2 (dois atuadores); com `actuator` 0 ou 1 e `term` 1..4 → descritor do atuador (pequeno: 1/0/1/0 ... a tabela exata fica no documento da Task 1; o que não estiver lá é "desconhecido" e falha com log).
  - `int InfoMode(int port, int slot, int term, int offset)`: `term` 1 = id corrente, 2 = id estendido, 3 = offset corrente, 4 = tabela de modos (`offset == -1` → quantidade 2: digital e analógico DualShock 2; `offset` 0/1 → `0x41`/`0x73`); `int SetMainMode(int port, int slot, int offset, int lock)` → 1 e troca o modo; `int InfoPressMode(int port, int slot)` → 1.
  - `int SetActAlign(int port, int slot, const uint8_t data[6])` → 1; `int SetActDirect(int port, int slot, const uint8_t data[6])` → 1 e chama `Rumble(port, data[0] ? 255 : 0, data[1])` somente quando o par muda desde a última chamada daquela porta; porta desconectada não chama o backend.

- [ ] **Step 1: Write the failing tests** `pad_device_tests.cpp` com `FakePadBackend` (estado programável e lista de chamadas de `Rumble`): um teste por linha do Review Focus 1, 2, 4 e 5 (inclui `Read` com modos 0x41/0x73/0x79 devolvendo 4/8/20 e `SetMainMode` mudando o modo; porta 2 e slot 1 falham; desconectado → `State==0`, `Read==0`); `SetActDirect` duas vezes com o mesmo par gera uma única chamada; `{0,0}` depois de `{1,200}` gera `Rumble(port, 0, 0)`.
- [ ] **Step 2: Run** `cmake --build build --target aoe2_pad_device_tests` — Expected: falha de compilação.
- [ ] **Step 3: Implement** `PadDevice` sem estado além de: portas abertas, modo corrente por porta, último par de vibração por porta.
- [ ] **Step 4: Run** `ctest --test-dir build -R pad_device --output-on-failure` — Expected: PASS.
- [ ] **Step 5: Commit** `feat: native libpad device model`.

---

### Task 4: Backend do host (raylib) e mapeamento de botões

**Files:**
- Create: `pad/include/aoe2/pad/host_button_map.hpp`, `pad/src/host_button_map.cpp`, `tests/cpp/host_button_map_tests.cpp`
- Create: `game_hooks/src/kcet_pad_backend.cpp` (backend raylib; não compilado nos testes do host)
- Modify: `CMakeLists.txt` (fonte do mapeamento, teste), `cmake/Aoe2Recompiled.cmake` (acrescentar só `game_hooks/src/kcet_pad_backend.cpp` às fontes de `aoe2_game`; o `kcet_pad.cpp` é acrescentado na Task 5, quando o arquivo passa a existir)

**Interfaces:**
- Produces: `enum class HostButton { kCross, kCircle, kSquare, kTriangle, kL1, kR1, kL2, kR2, kSelect, kStart, kL3, kR3, kUp, kDown, kLeft, kRight }` e `uint16_t ToPadButtons(const std::array<bool,16>& down_by_host_button)` (puro, testado). `std::unique_ptr<aoe2::pad::PadBackend> aoe2::game_hooks::MakeRaylibPadBackend()` em `game_hooks/include/aoe2/game_hooks/kcet_pad_backend.hpp` (gamepad 0 do raylib; `Sample(0)` lê botões e eixos; `Sample(1)` devolve desconectado; `Rumble` chama `SetGamepadVibration(0, large/255.0f, small ? 1.0f : 0.0f, 0.2f)` ou `StopGamepadVibration` se ambos forem zero; reaplica a vibração a cada 150 ms enquanto estiver ligada para cobrir a duração curta).

- [ ] **Step 1: Write the failing test** `host_button_map_tests.cpp`: nenhum botão → 0; só `kCross` → `0x4000`; só `kStart` → `0x0008`; os 16 botões → `0xFFFF`; cada `HostButton` aciona exatamente um bit distinto.
- [ ] **Step 2: Run** `cmake --build build --target aoe2_host_button_map_tests` — Expected: falha de compilação.
- [ ] **Step 3: Implement** o mapeamento e o backend raylib (A/B/X/Y → Cross/Circle/Square/Triangle segundo a convenção do raylib; D-pad, ombros, gatilhos digitais, sticks, L3/R3, Select/Start; os eixos do raylib vão direto para `HostPadState`, com gatilhos analógicos também liberando `kL2`/`kR2` acima de 0,5). A convenção exata dos botões do raylib fica comentada no arquivo.
- [ ] **Step 4: Run** `ctest --test-dir build -R host_button_map --output-on-failure` — Expected: PASS. O backend raylib só é verificado pelo build do jogo (Task 5).
- [ ] **Step 5: Commit** `feat: host gamepad button map and raylib pad backend`.

---

### Task 5: Handlers Sony nativos (`pad_device`) e execução

**Files:**
- Create: `game_hooks/src/kcet_pad.cpp`, `game_hooks/src/pad_device_entries.def`
- Create: `tests/test_native_pad_device_entries.py`
- Modify: `cmake/Aoe2Recompiled.cmake` (acrescentar `game_hooks/src/kcet_pad.cpp` às fontes de `aoe2_game`), `docs/sot/pad-libpad-semantics.md`

**Interfaces:**
- Consumes: `PadDevice`, `MakeRaylibPadBackend()`, `NativeModuleSwitches` (`Enabled("pad_device")`).
- Produces: `pad_device_entries.def` com uma linha `PAD_DEVICE_ENTRY(<papel>, 0x<endereço>)` por chamada Sony: `init` 0x10CCB0, `start` 0x10DEC0, `slot_max` 0x10DDF0, `port_open` 0x10CF10, `port_close` 0x10D0F8, `state` 0x10D2E0, `read` 0x10D260, `req_state` 0x10D3F8, `info_act` 0x10D480, `info_mode` 0x10D6C0, `set_main_mode` 0x10D7F8, `set_act_direct` 0x10D8B0, `set_act_align` 0x10D968, `info_press_mode` 0x10DBA8. O mapeamento endereço→API vem dos pontos de chamada no `kt_libpad.c` (confiança alta em `state`, `read`, `port_open`, `req_state`, `set_act_direct`, `set_act_align`, `set_main_mode`, `info_mode`; média em `init`, `slot_max`, `info_act`, `info_press_mode`; `start` é desconhecido). Cada handler lê `a0..a3` (inclusive os registradores que o decomp mostra como argumentos variádicos) e, em `read`/`set_act_*`, traduz o ponteiro guest com `getMemPtr`.

- [ ] **Step 1: Write the failing test** `test_native_pad_device_entries.py`: o `.def` tem 14 entradas, endereços únicos e alinhados a 4, e cada endereço aparece como chamada externa em `~/repos/aoe/src/kcet/kt_libpad.c` (`unittest.skip` se o decomp faltar).
- [ ] **Step 2: Run** `python3 -m unittest tests.test_native_pad_device_entries` — Expected: FAIL.
- [ ] **Step 3: Write** o `.def`, o `kcet_pad.cpp` (um handler por entrada, `ctx->pc = ra`, retorno em `v0`, instalador com o interruptor `pad_device` e a verificação `hasFunction` do plano do kernel) e rodar os testes do host.
- [ ] **Step 4: Build e rodar** `docker_build.py --diagnostics` e, com `pad_device=on`, `validate_desktop.py --run-seconds 90 --interval 30` usando a lista de trace do pad. Expected: o log nativo mostra `init`, `port_open` e `state` ao subirem; o `kt_libpad` gerado passa a chamar `read` (o que o baseline nunca fez); anotar em `pad-libpad-semantics.md` o que mudou (botões na `PadSlot`, ou o estado em que a máquina parou, com a evidência). Se `read` não for chamado, o estado devolvido pelo `PadDevice` não é o que o despachante espera: ajustar pela evidência da Task 1 e repetir, registrando cada tentativa.
- [ ] **Step 5: Commit** `feat: native libpad service handlers behind pad_device`.

---

### Task 6: Porta das funções puras e da API do motor do `kt_libpad`

**Files:**
- Create: `pad/include/aoe2/pad/pad_guest_memory.hpp` (interface `PadGuestMemory`: `Load8/16/32`, `Store8/16/32`, `ReadBytes(addr, out, n)`, `WriteBytes(addr, in, n)`), `pad/include/aoe2/pad/kt_libpad_model.hpp`, `pad/src/kt_libpad_model.cpp`, `tests/cpp/kt_libpad_pure_tests.cpp`, `tests/cpp/fake_pad_guest_memory.hpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `PadGuestMemory`, `PadDevice` (Task 3), os offsets de `PadSlot` do `kt_libpad.h` (constantes `kSlot*` copiadas dele, com os `static_assert` equivalentes por `offsetof`-constantes).
- Produces: `class KtLibpadModel` com construtor `KtLibpadModel(PadGuestMemory&, PadDevice&, KtLibpadGlobals)` (`KtLibpadGlobals { uint32_t work; uint32_t port_table; uint32_t step_table; uint32_t analog_default; }` = endereços guest de `D_4EA148`, `D_4EA158`, `D_38C7B8`, `D_38C7C8`) e funções livres puras: `int ScaleValue(int value, int scale, int min_value, int max_value)` (`func_00127D88`), `int ScaleWithThreshold(int value, int a, int b)` (`func_00127DC8`), `int ScaleClamp(int value, int min_value, int max_value)` (`func_00127E40`), `int ScaleByMode(int value, int mode, int a, int b, int c)` (`func_00127D30`), `bool IsPressureModeId(int id)` (`func_00128868`), `bool ExtendedModeFlag(int value)` (`func_00127C78`), `int EnumeratePorts(const int* slots_per_port, int start, uint16_t* out_pairs, int max_items, int port_count)` (`func_001262E8`). Métodos da API do motor, cada um citando seu símbolo: leitura de botões, pressionados, soltos, analógicos, `SetActuator(pad, actuator, value1, value2)` (`func_00126750`), `ClearActuator(pad, actuator)` (`func_00127288`), `SetRapid`, `SetPressMode`, `SetMode`, `Status` (`func_00126C30`), `Count` (`func_00127370`) e as demais da lista 19 a 48 do decomp.

- [ ] **Step 1: Write the failing tests** `kt_libpad_pure_tests.cpp`: vetores das funções puras tirados do decomp (por exemplo `ScaleValue(5, 0x100, 0, 0)` devolve `0xFF`; `ScaleValue(300, 0x100, 0, 100)` devolve `0xFF`; `ScaleValue(-5, 0x100, 0, 100)` devolve 0; `IsPressureModeId(0x79)` verdadeiro e `0x73` falso; `EnumeratePorts` com `{1,1}` e início 0 produz os pares `(0,0),(1,0)`, respeita `max_items`, e `out_pairs == nullptr` devolve 0); acessores sobre uma `PadSlot` montada no `FakePadGuestMemory` (o dublê registra leituras e escritas): botões, pressionados e soltos leem os offsets 0x00, 0x02 e 0x04; `SetActuator(0, 1, 7, 9)` grava `act_config[3]=9` e `act_config[4]=7` (offsets 0x55+3 e 0x55+4); `SetActuator` com atuador >= 6 dispara a asserção do decomp (linha 0x39B) e `pad_number` fora da faixa dispara a linha 0x398, ambas via `ReportAssertion` do serviço de log.
- [ ] **Step 2: Run** `cmake --build build --target aoe2_kt_libpad_pure_tests` — Expected: falha de compilação.
- [ ] **Step 3: Implement** as funções e os métodos citados, uma a uma, fiéis ao decomp, incluindo os retornos fixos em 0 (`func_001276B0`, `func_00128A18`).
- [ ] **Step 4: Run** `ctest --test-dir build -R kt_libpad_pure --output-on-failure` — Expected: PASS.
- [ ] **Step 5: Commit** `feat: native kt_libpad pure helpers and engine API`.

---

### Task 7: Porta da máquina de estados e da atualização por quadro

**Files:**
- Create: `pad/src/kt_libpad_state_machine.cpp`, `tests/cpp/kt_libpad_state_tests.cpp`
- Modify: `pad/include/aoe2/pad/kt_libpad_model.hpp`, `CMakeLists.txt`

**Interfaces:**
- Consumes: `KtLibpadModel`, `PadDevice`, `FakePadBackend`.
- Produces (métodos de `KtLibpadModel`): `void Init(int rpc_id, int pad_count, uint32_t slots_address, int flag)` (`func_00125C58`), `void InitSlot(uint32_t slot)` (`func_00125EC0`), `void Update()` (`func_00127380`), `void StepSlot(uint32_t slot)` (`func_001273D8`: despacha o passo `step` da tabela de 3 entradas lida do guest em `step_table`, avança o passo módulo 3 e executa `func_00125E20`), `bool OpenPort(uint32_t slot)` (`func_00127488`), `bool PollState(uint32_t slot)` (`func_001275A0`), `bool ClosePort(uint32_t slot)` (`func_00127750`), e os passos internos `func_001279A0` a `func_001279C8`, `func_00127E68` (processamento do relatório), `func_00128390` (vibração), `func_001281C0`, `func_001286A8`, `func_00128878`. A tabela de passos é lida do guest. O despacho por estado segue o binário, não o decomp, conforme a tabela de `docs/sot/pad-libpad-semantics.md` (5 → `func_001279B8`, 6 → `func_001279C0`, 7 → `func_00127BD8`, 2 → `func_001279B0`); isso é fixado em teste.

- [ ] **Step 1: Write the failing tests** `kt_libpad_state_tests.cpp` com `FakePadBackend`, `FakePadGuestMemory` e um `PadDevice` real: (a) `Init` com 2 pads zera as `PadSlot`, grava `req_port/req_slot` por `EnumeratePorts` e `n_pad` no trabalho; (b) o despacho por estado segue a tabela do binário (estados 0, 1, 2, 5, 6, 7 e 0x63 chamam as funções da tabela); (c) com o `FakePadBackend` apertando `kCross` e o modo analógico, depois de N chamadas de `Update` o campo `buttons` da `PadSlot` tem o bit `0x0040`, `buttons_pressed` o tem só no primeiro quadro em que apertou e `buttons_released` o tem só no quadro em que soltou; (d) `SetActuator(0, 0, 1, 200)` seguido de `Update` faz o backend receber `Rumble(0, 255, 200)` uma única vez, e `Update` repetido sem mudança não repete; (e) pad desconectado: botões zerados, `err_count` incrementa, nenhuma chamada de `Rumble`; (f) `StepSlot` com `step` fora de 0..2 dispara a asserção da linha 0x568.
- [ ] **Step 2: Run** `cmake --build build --target aoe2_kt_libpad_state_tests` — Expected: falha de compilação.
- [ ] **Step 3: Implement** as funções em `kt_libpad_state_machine.cpp` a partir do decomp. Onde os passos dependem de um retorno da libpad cuja semântica a Task 1 deixou "desconhecida", usar o valor fixado em `pad-libpad-semantics.md`.
- [ ] **Step 4: Run** `ctest --test-dir build -R kt_libpad_state --output-on-failure` — Expected: PASS.
- [ ] **Step 5: Commit** `feat: native kt_libpad state machine and per-frame update`.

---

### Task 8: Tabela de entradas, adaptador do módulo e interruptor `pad`

**Files:**
- Create: `game_hooks/src/kt_libpad_entries.def`, `tests/test_native_kt_libpad_coverage.py`, `game_hooks/src/kcet_libpad.cpp`
- Modify: `cmake/Aoe2Recompiled.cmake`, `docs/superpowers/specs/2026-10-24-kcet-native-layer-design.md` (estado do módulo)

**Interfaces:**
- Consumes: `KtLibpadModel`, `PadDevice`, `MakeRaylibPadBackend()`, o serviço de guest do adaptador do kernel (mesmo padrão: `Bind(rdram, ctx)`, `ctx->pc = ra`, retorno em `v0`).
- Produces: `kt_libpad_entries.def` com `KT_LIBPAD_ENTRY(<nome>, 0x<endereço>)` para todas as funções do módulo; as que dependem de algo fora dele ficam como `KT_LIBPAD_DEFERRED(endereço, motivo)` (por exemplo as funções de alocação e pausa de thread chamadas pelo módulo, que seguem como código gerado: `func_00128A68`, `func_00128A90`, `func_00128B18`, `func_00128B38`, `func_00128B68`, `func_00128B08` — o motivo registrado é a dependência em chamadas de thread e RPC que este plano não porta). Instalação só com o interruptor `pad` e exigindo `pad_device` ligado (senão, erro claro no log e nada é instalado). O endereço de cada global vem do decomp.

- [ ] **Step 1: Write the failing test** `test_native_kt_libpad_coverage.py`: usa `tools.trace_addresses.parse_function_addresses` sobre `kt_libpad.c` e exige que `ENTRY ∪ DEFERRED` seja exatamente o conjunto do decomp, sem duplicatas (`unittest.skip` se o decomp faltar).
- [ ] **Step 2: Run** `python3 -m unittest tests.test_native_kt_libpad_coverage` — Expected: FAIL.
- [ ] **Step 3: Write** o `.def` completo e o adaptador (um handler por entrada, estilo do `kcet_kernel.cpp`; funções com mais de quatro argumentos leem `t0..t3`).
- [ ] **Step 4: Run** os testes Python e do host — Expected: PASS.
- [ ] **Step 5: Commit** `feat: native kt_libpad adapter behind pad`.

---

### Task 9: Evidência de execução e registro

**Files:**
- Modify: `docs/sot/kcet-stall-log.md`, `docs/sot/pad-libpad-semantics.md`, `docs/superpowers/specs/2026-10-24-kcet-native-layer-design.md`

- [ ] **Step 1: Baseline** com os dois interruptores desligados: 90 s no build de diagnóstico, guardando as contagens de chamadas do trace do pad e o veredito.
- [ ] **Step 2: Rodar** com `pad_device=on`; depois com `pad_device=on` e `pad=on`. Expected em cada um: log `[native-...]` das chamadas esperadas; com `pad=on` nenhuma função do `kt_libpad` na lista de entradas aparece no trace como original chamado; as funções que o baseline chamou (init, enumeração de portas, atualização por quadro) aparecem nos contadores nativos com contagens equivalentes.
- [ ] **Step 3: Verificar** que a máquina de estados chega ao ramo de processamento de dados (log de `buttons` não nulo ao apertar um botão do controle real, se houver um conectado; senão, registrar que a verificação foi só com o dublê) e que a vibração chega ao backend (log do `Rumble`).
- [ ] **Step 4: Registrar** o resultado em `kcet-stall-log.md` (sem trechos do binário), inclusive se o jogo continuar parado, e marcar o módulo pad no design como "nativo, aguardando verificação diferencial".
- [ ] **Step 5: Commit** `docs: record native pad run evidence`.

---

## Em aberto

- **Semântica dos estados da libpad (resolvida na Task 1).** O original devolve 5 (executando comando) para sempre, porque o IOP de pad não existe; o binário mapeia 6 para o processamento de dados e o decomp tem os alvos de 5, 6 e 7 trocados. O dispositivo nativo reporta 6. Divergência do decomp registrada em `docs/sot/pad-libpad-semantics.md`; a Task 5 confirma em execução que `read` passa a ser chamado.
- A função `0x10DEC0` (chamada logo depois do init) e a identificação de `info_act`/`init`/`slot_max`/`info_press_mode` por uso são inferidas; a Task 5 confirma pelo comportamento.
- Funções de thread e RPC do módulo (`func_00128A68`, `func_00128A90`, `func_00128B18`, `func_00128B38`, `func_00128B68`, `func_00128B08`) seguem geradas; entram no plano do kernel/RPC.
- Backend do Switch (libnx, vibração HD): fora desta entrega; a interface `PadBackend` já o comporta.
- Teste diferencial contra o original e a pressão analógica real (hoje digital, 0 ou 0xFF).
- Relatório de asserção do original (função guest) segue só como log no nativo.

## Self-review

- **Cobertura do design:** módulo 2 da ordem aprovada; interruptor por camada; traço de evidência antes de assumir semântica; fidelidade ao decomp com quirks fixados; sem stubs. Vibração coberta nas Tasks 3, 4, 6, 7 e 9.
- **Consistência:** `PadBackend`, `PadDevice`, `PadReport`, `KtLibpadModel`, `PadGuestMemory` e os nomes dos `.def` aparecem com a mesma forma de uma tarefa para a outra.
- **Risco declarado:** a Task 1 e a Task 5 podem mudar a semântica de estados; as Tasks 3 e 7 dependem do que elas fixarem.
- **Proporção:** o plano é maior que o design; cada tarefa fixa interface e testes, e deixa os corpos ao implementador.
