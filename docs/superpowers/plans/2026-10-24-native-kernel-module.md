# Módulo nativo `kernel` (plano 1 de 8 da camada KCET nativa) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Substituir as funções do módulo `kt_kernel` do decomp (semáforos, threads, vsync) por código nativo do host, ligado por um interruptor de módulo, sem executar o original.

**Architecture:** Um modelo C++ puro (sem `R5900Context`) porta a lógica do decomp e fala com o mundo por uma interface `KernelServices`. Um adaptador em `game_hooks/` implementa essa interface sobre `EeScheduler` e memória guest, e registra um handler por endereço com `replaceFunction`. O estado do vsync continua nas globais guest do decomp, lidas e escritas pela interface. Os próximos planos (pad e vibração, entrada, arquivos e CD, cartão de memória, `kt_hd`, GPU, rede) reutilizam o interruptor e o padrão do adaptador.

**Tech Stack:** C++20, CMake/Ninja, ctest (testes executáveis simples com `Check`, como `tests/cpp/kcet_hook_config_tests.cpp`), Python `unittest`, `EeScheduler` do PS2Recomp pinado.

**Spec:** `docs/superpowers/specs/2026-10-24-kcet-native-layer-design.md` (aprovado em 2026-10-24); detalhes do módulo de arquivos em `docs/superpowers/specs/2026-10-23-file-module-native-shim-design.md`.

## Global Constraints

- Fonte de verdade: `~/repos/aoe/src/kcet/kt_kernel.c` (o decomp não pode ser modificado a partir daqui). Cada arquivo novo que porta lógica dele leva no topo a origem: repositório `samuelcaldas/age-of-empires-2-decomp`, commit (`git -C ~/repos/aoe rev-parse HEAD` no momento da escrita) e arquivo.
- Endereços guest vêm do decomp (nome `func_00134A20` = `0x00134A20`; globais `D_38CEC0`, `D_38CED0`, `D_490740`) e podem ir em arquivos versionados (autorização do dono de 2026-10-24). Binários, ISOs, exportações do Ghidra, mapas de função, disassembly e C++ gerado nunca entram no git.
- Sem stubs e sem retorno inventado: onde o decomp define o resultado, o nativo devolve o mesmo; onde não define, falha explícita com log.
- O interruptor padrão de todo módulo é **desligado**; só liga por arquivo de configuração.
- Não editar `third_party/PS2Recomp`, código gerado ou o decomp.
- Teste vermelho antes do código; um commit por tarefa, terminando com a linha `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`.
- Build do jogo só por `python3 tools/run_desktop_probe_build.py --jobs N`; testes do host por `cmake -B build -G Ninja && cmake --build build && ctest --test-dir build --output-on-failure`.
- Textos de log do kernel nativo são nossos; não são byte a byte os do original (só log, sem efeito no jogo).
- Estilo: 4 espaços, LF, namespaces `aoe2::<camada>`, cabeçalhos em `<camada>/include/aoe2/<camada>/`.

## Review Focus

1. Init do vsync chamado duas vezes para o mesmo modo: a segunda não cria semáforos nem registra handler de novo; após `Shutdown`, init volta a funcionar. (Task 3)
2. Falha ao criar semáforo no meio do intervalo: todos os semáforos do vsync já criados são apagados e o id negativo é devolvido. (Task 3)
3. Espera de vsync com id de semáforo negativo: registra erro e devolve 0 sem bloquear. (Task 3)
4. Modo de init fora de 1 e 2 (0, 3, -1): registra os dois handlers, como o `else` do decomp. (Task 3)
5. Arquivo de interruptores com módulo desconhecido, valor inválido, chave duplicada ou ausente: rejeitado com erro claro, e arquivo ausente deixa tudo desligado sem derrubar o boot. (Task 1)

---

### Task 1: Interruptores por módulo

**Files:**
- Create: `game_hooks/include/aoe2/game_hooks/native_module_switches.hpp`
- Create: `game_hooks/src/native_module_switches.cpp`
- Create: `tests/cpp/native_module_switches_tests.cpp`
- Modify: `CMakeLists.txt` (fonte em `aoe2_interfaces`; executável `aoe2_native_module_switches_tests`; `add_test(NAME native_module_switches ...)`)

**Interfaces:**
- Produces: `aoe2::game_hooks::NativeModuleSwitches` com `static NativeModuleSwitches Parse(std::istream&)` (lança `std::invalid_argument`), `static NativeModuleSwitches AllOff()`, `bool Enabled(std::string_view module) const`; constante `kNativeModuleNames` com `kernel, pad, mouse, keyboard, file_cd, memory_card, hd, gpu, net`.

- [ ] **Step 1: Write the failing test** `native_module_switches_tests.cpp`, com asserts: texto vazio → `Enabled("kernel") == false` para todos os nomes; `"# c\n\nkernel=on\nnet=off\n"` → kernel `true`, net `false`, pad `false`; `AllOff()` desligado. `Parse` rejeita (verificado com um `Rejects` como em `kcet_hook_config_tests.cpp`): `"kernel=maybe\n"`, `"unknown=on\n"`, `"kernel=on\nkernel=off\n"`, `"kernel\n"`, `"=on\n"`, `"kernel=\n"`. `Enabled("unknown")` lança `std::invalid_argument`.
- [ ] **Step 2: Run** `cmake -B build -G Ninja && cmake --build build --target aoe2_native_module_switches_tests` — Expected: falha de compilação (cabeçalho inexistente).
- [ ] **Step 3: Implement** a classe em `native_module_switches.cpp` (mapa nome→bool; linhas `nome=on|off`, `#` e linhas em branco ignorados; espaços ao redor aparados).
- [ ] **Step 4: Run** `ctest --test-dir build -R native_module_switches --output-on-failure` — Expected: PASS.
- [ ] **Step 5: Commit** `feat: per-module native switches` com os 4 arquivos.

---

### Task 2: Interface de serviços do kernel e dublê de teste

**Files:**
- Create: `kernel/include/aoe2/kernel/kernel_services.hpp`
- Create: `tests/cpp/fake_kernel_services.hpp`
- Modify: `CMakeLists.txt` (adicionar `kernel/include` a `target_include_directories(aoe2_interfaces PUBLIC ...)`)

**Interfaces:**
- Produces: `aoe2::kernel::KernelServices` (interface pura), todos `virtual ... = 0`:
  - `int CreateSemaphore(int init_count, int max_count, int option)` (negativo = erro), `int DeleteSemaphore(int id)`, `int SignalSemaphore(int id)`, `int SignalSemaphoreFromInterrupt(int id)`, `int PollSemaphore(int id)`, `int WaitSemaphore(int id)` — cada um devolve o id em sucesso, como os wrappers Sony do decomp.
  - `int AddIntcHandler(int cause, uint32_t guest_handler)`, `int RemoveIntcHandler(int cause, int handler_id)`, `void EnableIntc(int cause)`.
  - `uint32_t LoadWord(uint32_t guest_address)`, `void StoreWord(uint32_t guest_address, uint32_t value)`, `std::string LoadString(uint32_t guest_address)`.
  - `void Log(std::string_view message)`, `void ReportAssertion(std::string_view file, int line, std::string_view expression)`.
  - Threads: `struct ThreadInfo { uint32_t status, entry, stack, stack_size, gp, init_priority, current_priority, attr, option, wait_type, wait_id, wakeup_count; }`; `struct ThreadSpec { uint32_t entry, stack, stack_size, gp, init_priority, option; }`; `int CreateThread(const ThreadSpec&)`, `int StartThread(int id, uint32_t arg)`, `int TerminateThread(int id)`, `int DeleteThread(int id)`, `int CurrentThreadId()`, `int ReferThread(int id, ThreadInfo& out)`, `int ChangeThreadPriority(int id, int priority)`.
  - `struct SemaphoreInfo { uint32_t current_count, init_count, max_count, wait_threads, option; }`; `int ReferSemaphore(int id, SemaphoreInfo& out, bool from_interrupt)`.
- Produces: `FakeKernelServices` (em `tests/cpp/`), que registra cada chamada numa `std::vector<std::string> calls` ("signal 3", "wait 4" etc.), guarda memória guest num `std::map<uint32_t,uint32_t>`, entrega ids crescentes a partir de 1, e deixa o teste definir `fail_create_after` (falha a criação de semáforo depois de N sucessos).

- [ ] **Step 1: Write** os dois cabeçalhos (sem lógica além do dublê).
- [ ] **Step 2: Verify** `cmake --build build` compila (os testes das tarefas 3 e 4 os consomem).
- [ ] **Step 3: Commit** `feat: kernel services interface and test double`.

---

### Task 3: Modelo de semáforos e vsync

Porta de: criar/apagar/sinalizar/esperar semáforo, esvaziar, acessores do vsync, callbacks VBLANK_S/VBLANK_E, init/shutdown, espera de vsync e despejos de semáforos do decomp.

**Files:**
- Create: `kernel/include/aoe2/kernel/vsync_model.hpp`, `kernel/src/vsync_model.cpp`
- Create: `tests/cpp/vsync_model_tests.cpp`
- Modify: `CMakeLists.txt` (fonte em `aoe2_interfaces`; executável e `add_test(NAME vsync_model ...)`)

**Interfaces:**
- Consumes: `KernelServices` (Task 2).
- Produces: `aoe2::kernel::VsyncCallbacks { uint32_t vblank_start; uint32_t vblank_end; }` (endereços guest dos dois callbacks INTC) e `class VsyncModel` com construtor `VsyncModel(KernelServices&, VsyncCallbacks)` e métodos, todos devolvendo o que o decomp devolve:
  - `int Init(int mode)` (1 → só VBLANK_S, 2 → só VBLANK_E, outro → ambos), `void Shutdown()`.
  - `int WaitVblankStart()`, `int WaitVblankEnd()`, `int OnVblankStart()`, `int OnVblankEnd()` (os dois últimos devolvem 0).
  - `int VsyncSemaphore(int index)` para índices 0..3 (lê a global guest `0x38CEC0 + 4*index`).
  - `int CreateSemaphore(int init, int max, int option)`, `int SignalSemaphore(int id)`, `int SignalSemaphoreFromInterrupt(int id)`, `int DeleteSemaphore(int id)`, `int WaitSemaphore(int id)`, `int DrainSemaphore(int id)` (conta com `PollSemaphore` até <=0), `int DrainSemaphoreReporting(int id)`.
  - `void DumpSemaphore(int id, bool from_interrupt)`, `void DumpAllSemaphores(bool from_interrupt)` (ids 1..0x80).
  - Tabela de handlers guest em `0x38CED0` (pares causa/id; `-1` quando removido), exatamente como o decomp.

- [ ] **Step 1: Write the failing tests** em `vsync_model_tests.cpp` com `FakeKernelServices`:
  - `init_mode_2_creates_two_semaphores_and_registers_cause_3`: após `Init(2)`, `VsyncSemaphore(2)` e `VsyncSemaphore(3)` são >= 0, `VsyncSemaphore(0)` continua como estava, `calls` contém um `add_intc 3` com o endereço `vblank_end` e `enable_intc 3`.
  - `init_mode_1_registers_cause_2`: idem para o modo 1 (semáforos 0 e 1, causa 2, `vblank_start`).
  - `init_other_modes_register_both` para os modos 0, 3 e -1 (Review Focus 4).
  - `second_init_is_a_noop_until_shutdown` (Review Focus 1): segundo `Init(2)` não adiciona chamadas; depois de `Shutdown()` um novo `Init(2)` registra de novo; `Shutdown` remove os handlers, apaga os 4 semáforos e restaura `-1` nas globais.
  - `create_failure_rolls_back_all` (Review Focus 2): com `fail_create_after = 1` em `Init(1)`, o semáforo já criado é apagado, o retorno é negativo e nenhum handler é registrado.
  - `wait_vblank_end_signals_then_waits`: `calls` mostra `signal <sem2>` seguido de `wait <sem3>`; retorno é o do `WaitSemaphore`.
  - `wait_with_negative_semaphore_logs_and_returns_zero` (Review Focus 3): com globais em `-1`, `WaitVblankEnd()` devolve 0, chama `Log` e não chama `wait`.
  - `vblank_callback_drains_poll_and_signals_from_interrupt`: com o `PollSemaphore` do dublê devolvendo o id duas vezes e depois outro valor, `OnVblankEnd()` faz exatamente dois `signal_isr <sem3>` e devolve 0; com `sem2 < 0` não chama nada.
  - `drain_counts_until_poll_stops`: com 3 sinais pendentes, `DrainSemaphore` devolve 3; `DrainSemaphoreReporting` loga só quando a contagem é > 0.
  - `signal_mismatch_logs`: se `SignalSemaphore` devolve id diferente do pedido, `Log` é chamado e o retorno é o devolvido.
- [ ] **Step 2: Run** `cmake --build build --target aoe2_vsync_model_tests` — Expected: falha de compilação.
- [ ] **Step 3: Implement** `VsyncModel` portando `kt_kernel.c` função por função; flags `initialized` de cada vblank são membros (equivalem às `static` do decomp); estado em guest via `LoadWord`/`StoreWord`; cada função cita no comentário o símbolo do decomp.
- [ ] **Step 4: Run** `ctest --test-dir build -R vsync_model --output-on-failure` — Expected: PASS.
- [ ] **Step 5: Commit** `feat: native vsync and semaphore model from kt_kernel`.

---

### Task 4: Modelo de threads

Porta de: prioridade atual/de thread, mudar prioridade, terminar e apagar, criar, iniciar, criar e iniciar, status, nome do status e despejos.

**Files:**
- Create: `kernel/include/aoe2/kernel/thread_model.hpp`, `kernel/src/thread_model.cpp`
- Create: `tests/cpp/thread_model_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `KernelServices`, `ThreadSpec`, `ThreadInfo` (Task 2).
- Produces: `class ThreadModel` com construtor `ThreadModel(KernelServices&, uint32_t gp_value, uint32_t status_table_address)` (`gp_value` = endereço da global `D_4EA7C0`, `status_table_address` = `0x490740`, tabela de 6 pares `{status_id, ponteiro_para_texto}` lida do guest) e:
  - `int CurrentPriority()`, `int Priority(int id)`, `int ChangeCurrentPriority(int priority)`.
  - `int TerminateAndDelete(int id)` (loga se terminate ou delete devolver id diferente, devolve `id`).
  - `int Create(uint32_t entry, uint32_t stack, int stack_size, int priority, int option)` (reporta asserção `{arquivo do decomp, linha 0x93, expressão}` quando o id é negativo e devolve o id), `int Start(int id, uint32_t arg)` (asserção com linha 0xA8 quando o id devolvido difere; devolve `id`), `int CreateAndStart(...)`.
  - `int Status(int id)`, `std::optional<std::string> StatusName(int status)`, `void Dump(int id)`, `void DumpAll()` (ids 1..0x40).

- [ ] **Step 1: Write the failing tests** em `thread_model_tests.cpp`:
  - `create_passes_spec_and_gp`: `Create(0x1000, 0x2000, 0x800, 5, 0)` chama `CreateThread` com `entry=0x1000, stack=0x2000, stack_size=0x800, init_priority=5, option=0, gp=gp_value`.
  - `create_failure_reports_assertion_with_line_0x93`: dublê devolve -1; `ReportAssertion` recebe linha 0x93 e a função devolve -1.
  - `start_mismatch_reports_assertion_with_line_0xa8`.
  - `create_and_start_chains_and_returns_created_id`.
  - `terminate_and_delete_returns_id_and_logs_each_mismatch`.
  - `priority_of_current_uses_current_thread_id`: `CurrentPriority()` consulta `CurrentThreadId()` e depois `ReferThread`.
  - `status_name_reads_table_from_guest_memory`: com a tabela de 6 pares no dublê, `StatusName(s)` devolve o texto do par correspondente e `nullopt` para status desconhecido.
  - `dump_all_skips_unknown_status_and_covers_ids_1_to_0x40`.
- [ ] **Step 2: Run** `cmake --build build --target aoe2_thread_model_tests` — Expected: falha de compilação.
- [ ] **Step 3: Implement** `ThreadModel` a partir do decomp.
- [ ] **Step 4: Run** `ctest --test-dir build -R thread_model --output-on-failure` — Expected: PASS.
- [ ] **Step 5: Commit** `feat: native thread model from kt_kernel`.

---

### Task 5: Tabela de entradas e cobertura contra o decomp

**Files:**
- Create: `game_hooks/src/kernel_entries.def`
- Create: `tests/test_native_kernel_coverage.py`

**Interfaces:**
- Produces: `kernel_entries.def`, uma linha por função do módulo: `KERNEL_ENTRY(<nome>, 0x00134A20)` para as 38 funções portadas, e `KERNEL_DEFERRED(0x00134D80, "depende de dois auxiliares fora do módulo; segue como código gerado")` para a habilitação de INTC com trava. Nomes usados pelo adaptador (Task 6): `thread_priority_current`, `thread_priority`, `thread_priority_change`, `thread_terminate_delete`, `sema_create`, `thread_create`, `thread_start`, `thread_create_start`, `sema_drain_report`, `sema_drain`, `sema_signal`, `sema_signal_isr`, `sema_delete`, `sema_wait`, `vsync_sema_0` a `vsync_sema_3`, `vblank_start_callback`, `vblank_end_callback`, `vsync_remove_handlers`, `vsync_delete_semas`, `vsync_init_range`, `vsync_register_start`, `vsync_register_end`, `vsync_init`, `vsync_shutdown`, `vsync_wait_pair`, `vsync_wait_start`, `vsync_wait_end`, `thread_status`, `thread_dump`, `thread_status_name`, `thread_dump_all`, `sema_dump_isr`, `sema_dump_all_isr`, `sema_dump`, `sema_dump_all`. A correspondência de cada nome com o símbolo do decomp vem da ordem do arquivo `kt_kernel.c`.

- [ ] **Step 1: Write the failing test** `test_native_kernel_coverage.py`: usa `tools.trace_addresses.parse_function_addresses` sobre `~/repos/aoe/src/kcet/kt_kernel.c` (`unittest.skip` se o arquivo não existir) e exige que o conjunto de endereços do `.def` (entradas mais adiadas) seja exatamente o do decomp; exige 38 entradas e 1 adiada; exige nomes únicos.
- [ ] **Step 2: Run** `python3 -m unittest tests.test_native_kernel_coverage` — Expected: FAIL (arquivo `.def` inexistente).
- [ ] **Step 3: Write** o `.def` completo conferindo cada endereço contra `kt_kernel.c`.
- [ ] **Step 4: Run** o mesmo comando — Expected: PASS.
- [ ] **Step 5: Commit** `feat: kernel entry table and decomp coverage test`.

---

### Task 6: Adaptador sobre `EeScheduler` e registro dos handlers

**Files:**
- Create: `game_hooks/src/kcet_kernel.cpp`
- Modify: `cmake/Aoe2Recompiled.cmake` (acrescentar `game_hooks/src/kcet_kernel.cpp` às fontes de `aoe2_game`)
- Modify: `game_hooks/src/aoe2_call_trace.cpp` (nada instala sobre um endereço já substituído pelo nativo: ao instalar o traço, pular endereços de `kernel_entries.def` quando o interruptor do kernel estiver ligado)

**Interfaces:**
- Consumes: `VsyncModel`, `ThreadModel`, `KernelServices`, `NativeModuleSwitches`, `kernel_entries.def`.
- Produces: `class SchedulerKernelServices final : public aoe2::kernel::KernelServices` (construída com `PS2Runtime&`, `uint8_t* rdram`; método `void Bind(R5900Context&)` chamado em cada handler antes do uso, para o `bindMainContextForSyscall` que o upstream exige); instalação por `PS2_REGISTER_GAME_OVERRIDE("aoe2 native kernel", "AOK.ELF", 0, 0, install_native_kernel)`.

- [ ] **Step 1: Implement** `SchedulerKernelServices` mapeando cada método para `runtime.eeScheduler()` (`createSemaphore`, `deleteSemaphore(id, interruptSafe)`, `signalSemaphore(id, interruptSafe)`, `pollSemaphore`, `waitSemaphore`, `addIrqHandler(false, cause, handler, ...)`, `removeIrqHandler`, `setIrqCauseEnabled`, `createThread`, `startThread`, `terminateThread`, `deleteThread`, `changePriority`) e memória guest por `getMemPtr`. Onde o `EeScheduler` não tiver método equivalente (consulta de thread ou semáforo), usar o mesmo caminho que o syscall correspondente do upstream em `src/lib/Kernel/Syscalls/` e registrar no commit qual foi.
- [ ] **Step 2: Implement** o instalador: lê `native_modules.ini` do diretório do ELF (ausente ⇒ `AllOff()`; malformado ⇒ falha com a mensagem do `Parse`); se `kernel` estiver desligado, não faz nada e loga uma linha; se ligado, para cada `KERNEL_ENTRY` exige `runtime.lookupFunction(address) != nullptr` (senão lança com o nome) e chama `replaceFunction(address, handler)`. Cada handler lê `a0..a3` de `R5900Context`, chama o método do modelo, escreve o retorno com `setReturnU32` e define `ctx->pc = ra` (padrão de `ReturnToCaller` em `kcet_mouse_overrides.cpp`). Os dois callbacks INTC usam os endereços `vblank_start_callback` e `vblank_end_callback` do `.def` em `VsyncCallbacks`.
- [ ] **Step 3: Implement** contador por entrada e log `[native-kernel] <nome> n=<k>` nas 3 primeiras chamadas e em potências de 2 (mesma política de `aoe2_call_trace.cpp`).
- [ ] **Step 4: Build** `python3 tools/run_desktop_probe_build.py --jobs 4` — Expected: compila e linka `aoe2_game`; `ctest --test-dir build` continua passando.
- [ ] **Step 5: Commit** `feat: native kernel adapter over EeScheduler`.

---

### Task 7: Evidência de execução e registro

**Files:**
- Modify: `docs/sot/kcet-stall-log.md` (entrada nova, sem endereços nem nomes do binário)
- Modify: `docs/superpowers/specs/2026-10-24-kcet-native-layer-design.md` (estado do módulo kernel)

- [ ] **Step 1: Baseline** — com `native_modules.ini` ausente, rodar 60 s no build de diagnóstico e guardar contagens do traço (`[call-trace]`) das funções do kernel e o veredito do validador.
- [ ] **Step 2: Ligar** o módulo (`game/SLES-50282/native_modules.ini` com `kernel=on`, arquivo ignorado) e rodar os mesmos 60 s. Expected: log `[native-kernel] ...` para as mesmas 13 funções que o baseline chamou (criar semáforo, criar thread, iniciar thread, esperar semáforo, init de vsync); nenhuma linha `[call-trace]` para endereços do kernel. Se alguma função do baseline não aparecer como `[native-kernel]`, o gerador chama o original sem passar pela tabela: parar e reportar, o módulo não é dado como substituído.
- [ ] **Step 3: Comparar** o veredito do validador e a atividade do callback VBLANK_E com o baseline; registrar igualdade ou diferença, inclusive se o jogo continuar parado (isso não é falha desta entrega).
- [ ] **Step 4: Registrar** as medidas no `kcet-stall-log.md` e marcar o kernel como "nativo, aguardando verificação diferencial" no design.
- [ ] **Step 5: Commit** `docs: record native kernel run evidence`.

---

## Self-review

- **Cobertura do design:** interruptor por módulo (Task 1); kernel como primeiro da ordem aprovada (Tasks 3 a 6); traço que prova o original não chamado (Task 7, passo 2); sem stubs (restrição global); decomp com origem anotada (restrição global); teste diferencial explicitamente adiado (Task 7 registra "aguardando verificação diferencial"). Os módulos 2 a 8 da ordem têm planos próprios, a escrever depois deste.
- **Lacuna aceita:** `func_00134D80` fica gerada (adiada no `.def`) até existir o plano dos auxiliares Sony.
- **Consistência:** `VsyncCallbacks`, `KernelServices`, `ThreadSpec`, `ThreadInfo` e os nomes das entradas do `.def` aparecem com a mesma forma nas Tasks 2 a 6.
- **Proporção:** o plano é maior que o design, mas cada tarefa fixa interface e testes; corpos de função ficam com o implementador.
