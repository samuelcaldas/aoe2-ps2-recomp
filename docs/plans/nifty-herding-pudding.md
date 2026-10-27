# Plano: renderer nativo com texturas HD e resolução Switch

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Substituir o pipeline gráfico PS2/KCET por implementação nativa com GLES3, carregar texturas HD da edição AoE2 2013 com fallback PS2, e suportar resolução do Nintendo Switch (1280×720 handheld, 1920×1080 docked).

**Architecture:** Mapear os comandos de draw em `D_38C740` (`kt_gpu`), implementar backend GLES3 para `IRenderer`, decodificador SLP 2.0N e paletas JASC em C++, injetar `LooseHdAssetProvider` no caminho de desenho, e integrar suporte a Switch via Mesa/libnx.

**Tech Stack:** C++20, OpenGL ES 3.0 (EGL/Mesa/SDL2), CMake/Ninja, Python 3 unittest.

**Spec:** `docs/plans/nifty-herding-pudding.md`, `docs/architecture-hd-asset-decoupling.md`, `docs/asset-merge-design.md`, `docs/sot/gpu-semantics.md`.

## Global Constraints

- Trabalhar diretamente em `main`, sem criar worktrees (instrução explícita do usuário).
- Nunca commitar binários de jogo, ISOs, projetos Ghidra, mapas de funções ou C++ gerado (`CLAUDE.md`).
- Binário/código gerado tem autoridade máxima em caso de divergência com o decomp.
- Código do decomp (`../aoe`) pode ser portado/copiado diretamente em arquivos rastreados com atribuição.
- Patches a `third_party/PS2Recomp` salvos em `tools/patches/PS2Recomp/NNNN-<topic>.patch`.

---

### Task 1: Seam de draw: identificação e instrumentação dos handlers de D_38C740

**Files:**
- Modify: `game_hooks/src/kcet_gpu_module.cpp`
- Modify: `game_hooks/src/gpu_runtime_adapter.hpp`
- Modify: `gpu/src/kt_gpu_model.cpp`
- Modify: `gpu/include/aoe2/gpu/kt_gpu_model.hpp`
- Modify: `docs/sot/gpu-semantics.md`

- [ ] **Step 1: Instrumentar `KtGpuModel::ValidateCommand` e dispatch para registrar `kind` e payload de pacotes**
- [ ] **Step 2: Adicionar teste unitário de captura e formatação de comandos GPU em `tests/cpp/kt_gpu_model_tests.cpp`**
- [ ] **Step 3: Compilar e rodar testes de modelo GPU**
- [ ] **Step 4: Identificar no binário e decomp os endereços e propósitos dos 29 handlers de `D_38C740`**
- [ ] **Step 5: Solicitar decomp ao `decomp-worker` para handlers de desenho essenciais (PRIM, sprite, blit)**
- [ ] **Step 6: Atualizar `docs/sot/gpu-semantics.md` com a tabela dos handlers mapeados**
- [ ] **Step 7: Commit**

---

### Task 2: Registro dos componentes pendentes de HUD no CMake

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `cmake/Aoe2HostTests.cmake` (se existente) ou `CMakeLists.txt`

- [ ] **Step 1: Adicionar `renderer/src/hud_layout.cpp` e `input/src/hud_inverse_mapper.cpp` na lib `aoe2_interfaces`**
- [ ] **Step 2: Registrar `tests/cpp/hud_layout_tests.cpp` como ctest `hud_layout_tests`**
- [ ] **Step 3: Compilar e rodar `ctest -R hud_layout`**
- [ ] **Step 4: Commit**

---

### Task 3: Decodificador de Paletas JASC (`JascPalette`)

**Files:**
- Create: `assets/include/aoe2/assets/jasc_palette.hpp`
- Create: `assets/src/jasc_palette.cpp`
- Create: `tests/cpp/jasc_palette_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Escrever teste de parsing JASC-PAL (header ASCII `JASC-PAL`, `0100`, 256 cores RGBA)**
- [ ] **Step 2: Executar teste para verificar falha (arquivo ausente)**
- [ ] **Step 3: Implementar `JascPalette::Load(std::span<const uint8_t>)` e `GetColor(uint8_t index)`**
- [ ] **Step 4: Executar `ctest -R jasc_palette` e verificar sucesso**
- [ ] **Step 5: Commit**

---

### Task 4: Decodificador SLP 2.0N (`SlpDecoder`)

**Files:**
- Create: `assets/include/aoe2/assets/slp_decoder.hpp`
- Create: `assets/src/slp_decoder.cpp`
- Create: `tests/cpp/slp_decoder_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Escrever teste para parse de header SLP 2.0N e decodificação de frame simples**
- [ ] **Step 2: Executar teste para verificar falha**
- [ ] **Step 3: Implementar `SlpDecoder` com suporte aos comandos básicos (skip, draw, player color, shadow)**
- [ ] **Step 4: Executar `ctest -R slp_decoder` e verificar sucesso**
- [ ] **Step 5: Testar com arquivo real de interface `game/hd-edition/source/resources/_common/drs/interface/50100.slp` se disponível**
- [ ] **Step 6: Commit**

---

### Task 5: Backend de Renderização GLES3 (`GlesRenderer`)

**Files:**
- Create: `renderer/include/aoe2/renderer/gles_renderer.hpp`
- Create: `renderer/src/gles_renderer.cpp`
- Modify: `CMakeLists.txt`
- Modify: `renderer/include/aoe2/renderer/renderer.hpp`

- [ ] **Step 1: Escrever teste de ciclo de vida e estado do GlesRenderer (`Initialize`, `BeginFrame`, `EndFrame`, `Shutdown`)**
- [ ] **Step 2: Executar teste para verificar compilação/falha**
- [ ] **Step 3: Implementar `GlesRenderer` implementando `IRenderer` com shaders de quad e suporte a texturas**
- [ ] **Step 4: Implementar `UploadTexture(uint32_t id, uint32_t w, uint32_t h, const uint32_t* rgba)` e `DrawSprite(...)`**
- [ ] **Step 5: Executar testes de renderer e verificar sucesso**
- [ ] **Step 6: Commit**

---

### Task 6: Resolução e Mapeamento de Assets HD (`AssetResolver`)

**Files:**
- Create: `assets/include/aoe2/assets/asset_resolver.hpp`
- Create: `assets/src/asset_resolver.cpp`
- Create: `tests/cpp/asset_resolver_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Escrever teste unitário de resolução de recurso PS2 para asset HD (com fallback para ausente)**
- [ ] **Step 2: Implementar `AssetResolver` com cache de texturas e consulta ao `LooseHdAssetProvider`**
- [ ] **Step 3: Executar `ctest -R asset_resolver` e verificar sucesso**
- [ ] **Step 4: Commit**

---

### Task 7: Conexão da Seam de Draw com o Renderer Nativo e Provedor HD

**Files:**
- Modify: `platform/desktop/src/main.cpp`
- Modify: `game_hooks/src/kcet_gpu_module.cpp`
- Modify: `game_hooks/src/gpu_runtime_adapter.hpp`

- [ ] **Step 1: Inicializar `LooseHdAssetProvider` e `GlesRenderer` no `main.cpp` desktop**
- [ ] **Step 2: Interceptar os handlers de draw mapeados no M1 para direcionar ao `AssetResolver` + `GlesRenderer`**
- [ ] **Step 3: Aplicar fallback para a implementação PS2 gerada quando asset HD não estiver disponível**
- [ ] **Step 4: Executar ctests de integração**
- [ ] **Step 5: Commit**

---

### Task 8: Validação Desktop e Preparação Switch

**Files:**
- Modify: `tools/docker_build.py`
- Modify: `docs/plans/nifty-herding-pudding.md`

- [ ] **Step 1: Executar build diagnóstico desktop via `python3 tools/docker_build.py --platform desktop --jobs 4`**
- [ ] **Step 2: Executar suite de testes Python e ctests completos**
- [ ] **Step 3: Configurar guards Switch no `gles_renderer.cpp` (EGL / Mesa switch / libnx)**
- [ ] **Step 4: Validar build Switch via container `devkitpro/devkita64` quando aplicável**
- [ ] **Step 5: Registrar resultados finais e atualizar documentação**
- [ ] **Step 6: Commit**

