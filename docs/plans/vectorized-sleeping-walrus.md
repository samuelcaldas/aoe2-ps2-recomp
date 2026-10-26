# Plan: Substituição dos Overlays Recompilados por Código Decompilado (@aoe-decomp) com Memória Residente

## Context

Atualmente, o projeto utiliza recompilação estática (`tools/run_overlay_pipeline.py` via Ghidra + PS2Recomp) para transformar os 8 binários de overlay do PS2 (`SMENU_CD.BIN`, `DCOMP_CD.BIN`, `LOBBY_CD.BIN`, etc.) em mais de 12.000 arquivos C++ em `generated/overlays/`. Isso acarreta tempo excessivo de build (geração lenta do CMake e compilação pesada), alta fragilidade com renomeação de símbolos (`overlay_rename.h`) e colisões de instrução.
Além disso, no hardware original do PS2 (32MB RAM), 6 overlays precisavam disputar e sobregravar o mesmo endereço de memória guest (`0x01D98000`), sendo carregados sob demanda via Thread 7 (`func_0012B648`) pelo subsistema `kt_libcdvd_server` (SIF CDVDFSV `sid=0x80000593, fn=0x22`).
Como o host (PC Linux x86_64, Switch, Android) não possui restrições de memória de 32MB, e o repositório `@aoe-decomp` (`/home/samuelcaldas/repos/aoe/src/overlays/`) já possui todo o código C decompilado e testado para os 8 overlays, a meta é substituir a pipeline estática de 12.000 arquivos gerados pela integração direta do código C decompilado, mantendo as estruturas residentes na memória e registrando os pontos de entrada no runtime.

## Decisão de Arquitetura & Mapeamento de Memória

1. **Preservação de Endereços Guest e Despacho via Overlay ID:**
   - O código principal do ELF emite `jal 0x01D98000`, `jal 0x01D98080`, etc., esperando que o overlay ativo responda.
   - O `PS2Runtime` já possui o mecanismo `addOverlayRegion` e `registerOverlayFunction(overlayId, address, RecompiledFunction)`. Ao consultar um endereço em região de overlay (`0x01D98000`, `0x01ED7000`, `0x01F68000`), o runtime lê a palavra do ID do overlay ativo no cabeçalho `MWo3` da RAM guest (`0x01D98004`, `0x01ED7004`, `0x01F68004`).
   - Os overlays decompilados ficam todos residentes na memória do host simultaneamente (sem necessidade de swap de código em disco ou sobregravação destrutiva), e seus pontos de entrada são registrados para cada `overlayId`.
2. **Adaptação de ABI (Guest R5900Context ↔ C Decompilado):**
   - As funções decompiladas possuem assinatura `void* func(void* a0, void* a1, void* a2, void* a3)`.
   - Criar adaptadores inline que leem os registradores `a0..a3` de `R5900Context` (ou usam ponteiros traduzidos para `rdram`), invocam a função C decompilada do overlay, gravam o retorno em `v0` e atualizam `ctx->pc = ctx->getRegU32(31)`.
3. **Substituição na Build (CMake):**
   - Em `cmake/Aoe2Recompiled.cmake`, desligar os 9 targets de objetos estáticos gerados em `generated/overlays/` e substituí-los pela compilação direta de `overlays/decompiled/*.c` com seus wrappers C++.

## Fases de Execução

### Fase 1: Importação do Código Decompilado (@aoe-decomp)
- Importar `src/overlays/*.c` e `include/*_overlay.h` de `../aoe` para `overlays/src/` e `overlays/include/`.
- Adicionar wrappers/shims de tipos e dependências compartilhadas (`common.h`).

### Fase 2: Adaptador e Tabela de Registro de Overlays
- Criar `overlays/src/overlay_registry.cpp` implementando `RegisterGeneratedOverlays(PS2Runtime& runtime)`:
  - Configura as 3 regiões de overlay (`0x1d98000`, `0x1ed7000`, `0x1f68000`).
  - Registra os adaptadores para cada ID de overlay (`DCOMP=5`, `HIST=9`, `LOBBY=4`, `MO_OV=2`, `MULTIM=7`, `RMMMEM=3`, `SMENU=6`, `THELP=8`).

### Fase 3: Integração no CMake
- Atualizar `cmake/Aoe2Recompiled.cmake` para compilar o novo módulo `aoe2_decompiled_overlays` e ligá-lo a `desktop_runtime_probe` e `aoe2_game`.
- Remover a dependência de `tools/run_overlay_pipeline.py` e Ghidra para overlays.

### Fase 4: Testes e Verificação
- Teste unitário de registro e despacho de overlays (`tests/cpp/overlay_dispatch_tests.cpp`).
- Execução do teste de inicialização do probe: `desktop_runtime_probe_init`.
- Execução do jogo de diagnóstico observando o carregamento via Thread 7 e avanço dos frames.

## Arquivos Críticos

- `cmake/Aoe2Recompiled.cmake`
- `overlays/include/aoe2/overlays/*.h`
- `overlays/src/*.c`
- `overlays/src/overlay_registry.cpp`
- `game_hooks/src/kcet_hook_registry.cpp`
- `tests/cpp/overlay_dispatch_tests.cpp`

## Verificação End-to-End

1. `ctest --test-dir build/desktop-probe -R desktop_runtime_probe_init --output-on-failure`
2. `ctest --test-dir build/local -R overlay_dispatch_tests --output-on-failure`
3. `python3 tools/docker_build.py --platform desktop --diagnostics --jobs 4`
4. Execução do executável `aoe2_game` com display virtual (`Xvfb :99`), verificando no log o registro dos 8 overlays decompilados e o despacho bem-sucedido nas chamadas de overlay.
