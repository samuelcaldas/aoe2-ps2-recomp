# Plan: Otimizar Linkagem do Nintendo Switch (Linker Moderno + Modularização de Overlays)

## Context
A pipeline de compilação e linkagem para Nintendo Switch (`devkitA64` / `Switch.cmake`) atualmente enfrenta gargalos severos de tempo e consumo de memória (RAM) durante a fase de link:
1. O executável principal `aoe2_game.elf` vincula diretamente todos os 17.421 objetos do jogo base (`aoe2_generated_game_code`) somados a todos os objetos compilados dos 9 overlays de disco (`aoe2_overlay_*`), exigindo do linker a resolução simultânea de milhares de seções e símbolos em um único binário monolítico.
2. O batch padrão de Unity Build (`AOE2_GENERATED_UNITY_BATCH`) está configurado como 16, gerando mais de 3.600 arquivos-objeto `.o` intermediários que sobrecarregam descritores de arquivo, tabelas de símbolos e memória do processo de link.
3. No ecossistema devkitA64 / libnx, o linker utilizado por padrão pelo GCC (`aarch64-none-elf-ld.bfd`) pode ser otimizado configurando opções de link compatíveis (`-fuse-ld=lld`, `-fvisibility=hidden`) e agrupando translation units em batches maiores (64 por unity batch).

---

## FASE 1: Habilitar Linker Moderno (`lld`) e Otimizar Metadados & Unity Batch

### Arquivos-alvo:
- `cmake/Aoe2Recompiled.cmake`
- `docker/switch/Dockerfile` (garantir pacote `lld` / llvm tools no container de build do Switch caso necessário)

### 1.1 Configuração de Flags de Linker para Nintendo Switch:
No target `aoe2_game` (e nas bibliotecas de código gerado):
```cmake
if(NINTENDO_SWITCH)
    # Utiliza lld caso disponível no toolchain devkitA64
    target_link_options(aoe2_game PRIVATE -fuse-ld=lld)
    # Oculta visibilidade padrão dos símbolos gerados para não poluir a tabela dinâmica
    target_compile_options(aoe2_generated_game_code PRIVATE -fvisibility=hidden)
endif()
```
*Observação técnica:* Também aplicar `-fvisibility=hidden` aos alvos de overlays para evitar inchar a tabela `.dynsym`.

### 1.2 Ajuste do Unity Batch Size:
Em `cmake/Aoe2Recompiled.cmake`:
- Ajustar `AOE2_GENERATED_UNITY_BATCH`: se estiver compilando para `NINTENDO_SWITCH` ou configurado para 64:
```cmake
if(NINTENDO_SWITCH AND AOE2_GENERATED_UNITY_BATCH EQUAL 16)
    set(AOE2_GENERATED_UNITY_BATCH 64 CACHE STRING "Generated units per unity translation unit" FORCE)
endif()
```
Isso reduz o número de objetos `.o` de ~3.664 para menos de 950, diminuindo brutalmente o overhead de file descriptors e I/O de disco durante a linkagem.

### 1.3 Critério de Aceite da Fase 1:
Executar no container:
```bash
docker run --rm -v $(pwd):/src -w /src/build/docker-switch aoe2-build-switch ninja -t commands aoe2_game | grep -E "fuse-ld=lld"
```
O comando gerado para o link do executável `aoe2_game` deve conter `-fuse-ld=lld`.

---

## FASE 2: Desacoplar os Overlays do Binário Principal

### Arquivos-alvo:
- `cmake/Aoe2Recompiled.cmake`
- `CMakeLists.txt`
- `game_hooks/src/kcet_cdvd_module.cpp` / `game_hooks/src/kcet_hook_registry.cpp`
- `generated/overlays/overlay_all.cpp` (ou gerador `tools/overlay_registration.py`)

### 2.1 Separação dos Overlays no Build (`AOE2_MODULAR_OVERLAYS`):
- Introduzir a opção no CMake:
  ```cmake
  option(AOE2_MODULAR_OVERLAYS "Build overlays as separate loadable payloads/modules instead of monolithic linking" OFF)
  if(NINTENDO_SWITCH)
      set(AOE2_MODULAR_OVERLAYS ON CACHE BOOL "Build modular overlays on Switch" FORCE)
  endif()
  ```
- Quando `AOE2_MODULAR_OVERLAYS` estiver ativo:
  - Não adicionar `<TARGET_OBJECTS:aoe2_overlay_*>` em `aoe2_link_recompiled(aoe2_game)`.
  - Compilar cada overlay como um módulo ou biblioteca estática/payload independente (ex: `aoe2_overlay_0X` gerando `.so` ou biblioteca estática modular / payload em `romfs_aoe2/overlays/`).
  - O executável principal `aoe2_game` fica restrito ao código base do `AOK.ELF` (`generated/SLES-50282/`), reduzindo drasticamente o pico de RSS e o tempo de linkagem do ELF principal.

### 2.2 Desacoplamento da Inicialização e Carregamento sob Demanda:
- No modelo monolítico atual, `RegisterGeneratedOverlays(runtime)` registra antecipadamente todos os ponteiros de função de todos os 8 overlays nos maps da `PS2Runtime`.
- No modo `AOE2_MODULAR_OVERLAYS`:
  - Registrar as regiões de overlay no runtime (`addOverlayRegion`), mas deferir a população dos ponteiros de função para quando o segmento de código do overlay for efetivamente lido ou demandado.
  - No interceptador do CDVD / leitura de disco (`Handler_sceCdRead` / `kt_libcdvd_server` em `kcet_cdvd_module.cpp`): ao carregar um arquivo de overlay (`*_CD.BIN`) na RAM guest, acionar a inicialização/registro do respectivo módulo de overlay (`RegisterOverlay_<name>(runtime)` ou payload dinâmico).

---

## REGRAS OBRIGATÓRIAS E VALIDAÇÃO

1. **Preservação do Build Desktop**:
   - `cmake -B build -G Ninja && cmake --build build --target aoe2_smoke_test` deve continuar compilando e passando sem alterações de comportamento.
2. **Controle de Concorrência**:
   - Respeitar o limite de memória da máquina host e usar o paralelismo configurado (`-j` controlado).
3. **Commits Limpos**:
   - Commit 1: `build: use lld and increase unity batch size for switch`
   - Commit 2: `arch: decouple overlays into loadable modules`
   - Realizar as alterações diretamente no branch `main` conforme solicitado.
