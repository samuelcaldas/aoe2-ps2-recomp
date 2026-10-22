# Plan: integrar a camada KCET ao runtime upstream (PS2Recomp)

## Context

As três plataformas compilam em Docker, mas o jogo **não progride**: o ELF carrega, os 4 hooks de mouse são aplicados, o guest começa no entry point e o log silencia depois de uma textura. A tela mostra só o fallback MAGENTA do runtime (`ps2_runtime.cpp` ~417), que significa "o guest ainda não apresentou nenhum frame". Os validadores atuais passam por "processo vivo", o que esconde isso. A meta é integrar **todos** os módulos KCET (mouse, teclado, pad, cdvd, file, mc, kernel, gpu, hd) via os pontos de extensão do upstream, sem editar `third_party/PS2Recomp`, código gerado, nem usar stubs com retorno inventado. "KACET" foi interpretado como KCET.

## Achados que moldam o plano

1. **Hipótese principal do travamento (a confirmar):** o ELF é stripped, então as funções Sony (`sceSif*`, `scePad*`, `sceCd*`, `sceMc*`) são código guest recompilado que fala SIF RPC com serviços do IOP que quase não existem. O IOP upstream só serve `mcserv` (0x80000400), `dbcman` e `libsd`. Não há servidor para padman, cdvd, USB mouse ou teclado. `SifBindRpc` sem rota deixa `server=0`, e um guest que espera o bind gira para sempre. O `kt_keybd` é um clone do `kt_mouse` (SID 0x80000211) e roda no boot, e **não está hookado**.
2. **Mecanismos upstream a reutilizar:** `PS2_REGISTER_GAME_OVERRIDE` (aplicado em `loadELF`, depois do preenchimento da tabela gerada, antes de `run()`), `PS2Runtime::replaceFunction` (só dentro de `[0x100008, 0x4d7140)`, alinhado a 4) e `bindAddressHandler(runtime, addr, "nome")`, que **alias um endereço guest a um handler upstream real** (`scePadRead`, `sceMcOpen`, `SifBindRpc`...). Pad, memory card, CD/VFS, fio, `EeScheduler` e o GS (emulação GIF/GS real) já existem upstream.
3. **`kt_gpu` não é o renderer.** Tem só 2 chamadores diretos (um quad de splash/fade). O renderer principal desenha superfícies 8-bit paletizadas em software e o frame chega ao GS por DMA/image transfer. O único seam real é `gs().setRasterBackend`. O ADR 0001 e o `architecture.md` dizem o contrário e serão corrigidos.
4. **`symbols.txt` não ajuda** (só nomes `ktMouse*`). Os âncoras estáveis estão no próprio ELF: strings `__FILE__` `kt_*.c`, constantes de SID, strings `ktKeybd*/ktMc*/ktCd*`, e o corpo dos wrappers Sony.
5. Os intervalos de módulo em `kcet_hardware_analysis.md` do decomp estão errados em alguns pontos (kt_gpu, kt_hd, fim do kt_usbkeybd_drv). Usar as âncoras do ELF, não a tabela.

## Ordem de trabalho (TDD: RED, depois GREEN; um commit por fatia)

**M0. Validação honesta e diagnóstico** (antes de qualquer hook novo)
- `tools/frame_analysis.py` (stdlib; envolve `magick`/`convert`, executor injetável): `SOLID_FALLBACK` vs `REAL_FRAME` por proximidade a (255,0,255) e contagem de cores. Testes em `tests/test_frame_analysis.py`.
- Estender `tools/classify_diagnostics.py` com veredictos de log: `PROGRESSING`, `STALLED_IN_LOOP`, `MISSING_FUNCTION`, `NO_EVIDENCE` (pc de `[run:tick]` avançando, eventos de bind SIF repetidos, `reportMissingFunction`).
- `tools/validate_desktop.py` e `validate_android.py` só passam com frame real, dois screenshots distintos e veredicto `PROGRESSING`. Esperado: **ficam RED no build atual** (baseline).
- Opção `AOE2_RUNTIME_DIAGNOSTICS` em `cmake/Aoe2Recompiled.cmake` (hoje força `AGRESSIVE_LOGS=OFF`): liga `RUNTIME_LOGS`, `AGRESSIVE_LOGS`, `IOP_RPC_TRACE`; `game_hooks/src/aoe2_diagnostics.cpp` chama `setMissingFunctionPolicy(Stop)` só nesse build. Flag `--diagnostics` em `tools/docker_build.py`, build dir separado.
- Capturar 60 s no desktop; registrar a tabela de decisão (SIDs e papéis, sem endereços nem nomes do binário) em `docs/sot/kcet-stall-log.md`.

**M1. Resolvedor ancorado no ELF** (`tools/kcet_resolver/`: `elf_image`, `mips_scan`, `anchors`, `catalog`, `verify`, `ini_writer`)
- Âncoras: string (função que contém o par `lui/addiu`, prólogo `addiu sp,sp,-N` anterior), SID (função que passa o SID ao wrapper de bind), wrapper Sony por estrutura. Falha se houver 0 ou mais de 1 candidato.
- `verify`: endereço alinhado, dentro do range da tabela e presente em `analysis/generated/function_map.csv`.
- Saída `game/SLES-50282/kcet_hooks.ini` (ignorado) com dois tipos: `kcet.<modulo>.<papel>=addr` (handler próprio) e `alias.<nomeUpstream>=addr` (via `bindAddressHandler`).
- `tools/resolve_kcet_hooks.py` vira CLI fina; o resolvedor atual de mouse fica como verificação cruzada. Testes com ELF sintético montado à mão + teste-guarda que impede endereços/nomes do jogo em arquivos versionados.

**M2. Registro de hooks e divisão por módulo** (`game_hooks/`)
- `kcet_hook_registry.{h,cpp}`: tabela nome→handler, leitor do ini (falha em linha malformada, duplicata, chave desconhecida; ini ausente é erro com instrução), `applyAll(runtime)` com `replaceFunction` para `kcet.*` e `bindAddressHandler` para `alias.*`. Uma única `PS2_REGISTER_GAME_OVERRIDE`.
- Mover o mouse atual para `kcet_mouse.cpp`. Arquivos por módulo (`kcet_keybd`, `kcet_pad`, `kcet_cdvd`, `kcet_file`, `kcet_mc`, `kcet_kernel`) **só quando a evidência exigir handler próprio**. "Todos os módulos" = cada módulo com disposição resolvida e documentada (alias, próprio, ou confirmado não usado), não código próprio para todos.
- Teste ctest do registro em `tests/cpp/`.

**M3. Destravar, um passo por vez, guiado pelo log**
1. Aliasar os 3 wrappers SIF Sony aos handlers upstream `SifInitRpc/BindRpc/CallRpc`; ver quais SIDs ainda ficam com `server=0`. Se não mudar nada, o log diz, e a causa é outra.
2. Para SIDs sem servidor (teclado 0x80000211, padman 0x80000100/101, cdvd 0x80000592..59A): preferir aliasar a entrada de alto nível (`scePad*`, `sceCd*`) ao handler upstream; para wrappers KCET direto sobre SIF (keybd, tabelas `ktCd*`), handler próprio com modelo testado (como `UsbMouseRpcModel`). Nunca retorno fabricado.
3. `kt_mc_2`: tentar só alias (SID 0x80000400 já é servido) e conferir dados em `mc0/`. `kt_kernel`/`kt_file`: provavelmente cobertos por `EeScheduler`/`fio*`. `kt_hd`: resolver o range; se for só dev-kit, registrar "não usado" com evidência e um guarda que falha se for atingido. `kt_gpu`: sem hook (roda dentro do GS upstream).
4. Reexecutar o diagnóstico após cada passo e anexar ao log.

**M4. Entrada**
- Teclado: `UsbKeyboardRpcModel` em `input/` sobre `GuestKeyboardAdapter`/`DiffPressedKeys`, tabela raylib→USB HID, setters de LED/caps guardam estado. Testes C++ (press/release, modificadores, rollover de 6, ida e volta de LED, cobertura da tabela). Confirmar o layout exato do relatório pelo lado guest durante M3.
- Gamepad: se o alias `scePad*` funcionar, o upstream (raylib gamepad 0, fallback de teclado) já cobre; handler próprio só se o bind do padman não puder ser aliasado.
- Android: teclado de hardware via NativeActivity (teclado virtual não é confiável). Switch: gamepad é a entrada principal; teclado/mouse só no dock.

**M5. Renderização**: aceitar a saída do GS upstream se o validador mostrar frames reais. Corrigir ADR 0001 e `architecture.md` (kt_gpu não é o seam). Backend GLES3 via `gs().setRasterBackend` fica como marco seguinte, fora desta meta.

**M6. Validação por plataforma**
- Desktop (Docker+Xvfb): critérios do M0 mais teste de entrada com `xdotool` (tecla/clique muda o frame).
- Android (emulador KVM): mesmo analisador sobre `adb exec-out screencap`; entrada com `adb shell input keyevent/tap/swipe`; gamepad fica manual.
- **Switch (manual, Atmosphère):** roteiro em `docs/switch-manual-test.md` e `tools/check_switch_evidence.py`. Passos: copiar o NRO para `/switch/aoe2/` com os dados do disco e `mc0/` gravável; rodar em modo aplicativo; capturar tela aos 10/30/60 s; navegar o menu com gamepad; salvar, sair, listar `mc0/` e recarregar; coletar log do SD, screenshots, listagem e crash report. Passa com frame não magenta, navegação por gamepad e save persistente.

## Arquivos críticos

- Modificar: `cmake/Aoe2Recompiled.cmake`, `tools/docker_build.py`, `tools/classify_diagnostics.py`, `tools/validate_desktop.py`, `tools/validate_android.py`, `tools/resolve_kcet_hooks.py`, `game_hooks/src/kcet_mouse_overrides.cpp` (vira `kcet_mouse.cpp`), `CLAUDE.md`, `docs/adr/0001-kcet-boundary-replacement.md`, `docs/architecture.md`, `docs/desktop-runtime-probe.md`.
- Criar: `tools/frame_analysis.py`, `tools/kcet_resolver/*`, `tools/check_switch_evidence.py`, `game_hooks/{include,src}/kcet_hook_registry.*`, `game_hooks/src/aoe2_diagnostics.cpp`, `input/*/usb_keyboard_rpc_model.*`, `input/src/hid_usage_table.cpp`, `docs/sot/kcet-stall-log.md`, `docs/switch-manual-test.md`, e os testes correspondentes.
- Reutilizar: `PS2_REGISTER_GAME_OVERRIDE`, `replaceFunction`, `bindAddressHandler`, `setMissingFunctionPolicy`, `UsbMouseRpcModel`, `GuestKeyboardAdapter`, `DiffPressedKeys`, `tools/verify_function_map.py` (padrão de ELF/CSV), `analysis/generated/function_map.csv`.

## Verificação

- `python3 -m unittest discover -s tests` e `ctest` (via `tools/docker_build.py`) passam, incluindo resolvedor, analisador, classificador, validadores, registro e modelo de teclado.
- O resolvedor, no ELF real, gera `kcet_hooks.ini` com todos os papéis do catálogo resolvidos e verificados.
- `validate_desktop.py` e `validate_android.py` passam com frame real, screenshots distintos e veredicto `PROGRESSING`, mais o teste de entrada.
- O pacote de evidências do Switch passa em `check_switch_evidence.py` e nos critérios manuais.

## Riscos

- **O travamento pode não estar no SIF** (vsync, semáforo, DMA). A ordem evidência-primeiro trata isso; se a causa estiver no upstream, que não podemos editar, reportamos em vez de remendar.
- **Convenção de chamada do alias** pode diferir do handler upstream: validar cada alias com um trace antes de manter.
- **Detecção ambígua de wrappers stripped:** o resolvedor falha de propósito; adicionar âncoras, não relaxar.
- **`replaceFunction` fora do range é ignorado:** `verify` precisa falhar nesse caso.
- **Build de diagnóstico dobra o tempo de compilação:** usar diretório de build separado e em cache.
- **Mouse em Android/Switch não é garantido:** documentar o toque como trabalho futuro.

---

# Adendo (2026-10-22): rede i.LINK pelo código decompilado

## Context

O jogo registra `-2000` em `sceILsockBind`/`Connect` e a thread receptora gira em modo de descoberta sem nunca conectar. O log mostra o motivo: `ILINK.IRX`, `ILSOCK.IRX` e `ILINT.IRX` (de `MODULES2`) são carregados no emulador de IOP e registram servidores RPC, mas não há hardware FireWire por trás, então as respostas voltam iguais aos pedidos. **Correção de uma inferência anterior** gravada no log de stall ("serviço ausente"): o serviço existe e roda, o que falta é o hardware. A causa do travamento de boot **não está provada** como sendo a rede (a análise estática diz que a descoberta não bloqueia a máquina de estados); este adendo corrige a rede por mérito próprio (multijogador LAN, ADR 0001, autorização do dono de 2026-10-22 para portar o decomp) e mede o efeito no boot.

## Decisão de design

Substituir **no lado EE**, na fronteira `sceILsock*` (open, bind, connect, send, recv, recvfrom, close), por funções do host via `replaceFunction`, com UDP real por trás. Isso evita editar `third_party/PS2Recomp` (que é fixado e o checkout já diverge do lock) e evita o ABI interno de RPC. Os módulos IRX do disco não são trocados nem alterados. A lógica DirectPlay (fila, jogadores, protocolo de 492 bytes) continua como código guest recompilado. Alternativa descartada: serviço HLE novo em `ps2xIOP` (exige alterar a dependência vendorizada).

## Etapas (TDD; RED antes de GREEN; um commit por fatia)

**N0. Especificação dos wrappers (bloqueia o resto).** O decomp não tem `socketee.c`. Delegar à sessão `aoe-decomp` (somente leitura, resposta em mensagem) a decompilação de: open, bind, connect, send, recv, recvfrom, close e do leitor de EUI-64. Por função: argumentos, retorno, códigos de erro (`-2000`, `-1000`, `-2007`), tratamento de bloqueio, formato do registro de endereço de 16 bytes (comprimento, família 60, porta, dois words de nó) e onde o comprimento recebido é devolvido. Não codificar nada sem esta saída.

**N1. Modelo puro e testável** `net/` (namespace `aoe2::net`, estilo `UsbMouseRpcModel`): tabela de sockets, regras de erro de `socketee`, endereço i.LINK ↔ `IPv4:porta` (EUI-64 → mapeamento determinístico; nó todo-1 → broadcast UDP), moldura fixa de 492 bytes com preenchimento determinístico. Interface `INetworkTransport` (do SOT) com backend de sockets POSIX em `platform/` e um backend em memória para testes. Testes em `tests/cpp/net_tests.cpp` (ctest `net_tests`): 0/1/muitos sockets, bind duplicado, envio de 493 bytes → `-2007`, recvfrom sem dados, loopback entre dois modelos.

**N2. Resolvedor.** Novo `tools/kcet_resolver/network.py`: âncoras pelas constantes de SID (`0x80000b88`, `b8a`, `b8b`, `b8f`, etc.) e pelas strings `socketee.c`; exige candidato único e início de função verificado no `function_map.csv`; falha em ambiguidade. Papéis `kcet.net.open|bind|connect|send|recv|recvfrom|close` no `kcet_hooks.ini` (ignorado). Estender o analisador estrito em `game_hooks/src/kcet_hook_config.cpp` e os testes Python com ELF sintético.

**N3. Handlers** `game_hooks/src/kcet_net.cpp`: lê argumentos de `R5900Context` (a0..a3), traduz ponteiros com `getMemPtr`, chama o modelo, escreve o resultado com `setReturnU32`. Recepção **não bloqueante**: o despacho é síncrono no thread guest e um handler que bloqueia trava o runtime; sem dados, devolve o código de "sem pacote" definido em N0 (a thread receptora já ignora retornos diferentes de 492).

**N4. Medição (critério de sucesso real).** Rodar 60 s e depois 500 s com o binário de diagnóstico: (a) as linhas `-2000` de `mydplay.cpp` desaparecem; (b) a thread receptora deixa de repetir `recvfrom` com o mesmo SID em loop apertado ou passa a consumir dados; (c) o screenshot muda ou não. Registrar o resultado, inclusive se a tela **não** mudar, em `docs/sot/kcet-stall-log.md` sem endereços.

**N5. LAN real.** Dois processos no mesmo host (loopback) e depois duas máquinas: descoberta por broadcast, `SESSION`/`JOINED` com quadros de 224 e 456 bytes lógicos. Sem captura real de dois PS2 existente, a fidelidade de bytes na rede não é verificável; documentar isso.

## Arquivos críticos

- Criar: `net/include/aoe2/net/*`, `net/src/*`, `platform/desktop/src/udp_transport.cpp`, `game_hooks/src/kcet_net.cpp`, `tools/kcet_resolver/network.py`, `tests/cpp/net_tests.cpp`, `tests/test_kcet_resolver_network.py`.
- Modificar: `CMakeLists.txt` (biblioteca e teste), `game_hooks/src/kcet_hook_config.cpp`, `tools/resolve_kcet_hooks.py`, `docs/sot/kcet-stall-log.md` (corrigir a inferência anterior), `docs/sot/multiplayer-lan-architecture.md`.
- Reutilizar: padrão de `input/include/aoe2/input/usb_mouse_rpc.hpp`, `game_hooks/src/kcet_hook_registry.cpp` (`replaceFunction`), `tools/kcet_resolver/mouse.py` (âncoras por SID), `PS2_REGISTER_GAME_OVERRIDE`.
- O código do decomp serve de especificação; o port é reescrito sobre `GuestMemory`, porque ele usa ponteiros de 32 bits e globais em endereços fixos e não compila direto no host (principalmente ARM64/Switch).

## Verificação

- `python3 -m unittest discover -s tests` e `ctest` (inclui `net_tests`) passam.
- `resolve_kcet_hooks.py` no ELF real resolve os 7 papéis de rede, cada um com início de função verificado.
- Execução de 60 s: sem `-2000` de bind/connect; `th_Receiver` ativo; relatório do N4 anexado ao log.
- Teste de loopback entre dois processos troca a descoberta `SESSION`/`JOINED`.

## Riscos

- **A rede pode não ser a causa do boot parado.** Medir e reportar, sem declarar sucesso pela ausência dos erros.
- **Semântica de bloqueio do recvfrom real é desconhecida** até N0; um retorno "sem dados" errado pode manter a thread em loop. Definir pelo decomp, não por palpite.
- **Sem captura de dois PS2:** bytes de preenchimento e ordem de bytes no fio não verificáveis.
- **Estado interno dos clientes RPC no guest** fica sem uso quando o wrapper é substituído por inteiro; confirmar em N4 que nada mais lê esses buffers.
- **Switch/Android:** o backend POSIX precisa de adaptação por plataforma; fora do escopo desta fatia.
