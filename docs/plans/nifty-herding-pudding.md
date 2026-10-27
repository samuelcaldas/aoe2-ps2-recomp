# Plano: GLES3 nativo em 1080p com assets HD Edition

## Contexto

O pedido é corrigir o resultado visual: a interface PS2 de baixa resolução continua ampliada, apesar de a janela abrir em 1920×1080 e de o log anunciar GLES/HD. A captura `.playwright-mcp/campaign-mission.png` confirma o problema. Em `game_hooks/src/kcet_gpu_module.cpp`, `NativeDrawHook` sempre retorna `false`; inicializar `GlesRenderer` e `AssetResolver` não demonstra que desenhem pixels apresentados.

Este plano refina e substitui a lista anterior, marcada como concluída sem comprovar o objetivo visual. Componentes existentes serão reaproveitados, mas integração, rasterização, layout e validação precisam ser demonstrados separadamente. Nenhuma tarefa abaixo foi executada nesta fase de planejamento.

**Objetivo:** renderizar a primeira missão de William Wallace em GLES3, com alvo real 1920×1080, interface HD recomposta e interativa, texto rasterizado na resolução nativa e recursos do HD Edition efetivamente visíveis. Suportar também 1280×720 sem depender de ampliar o framebuffer PS2.

## Restrições

- Trabalhar diretamente em `main`, sem worktrees e sem PRs.
- Não alterar simulação, regras, saves, rede ou comportamento de entrada para disfarçar problemas gráficos.
- Não editar C++ gerado; decomp autorizado é referência, e divergências são resolvidas pelo binário/código gerado.
- Não modificar o repositório de decomp a partir daqui. Portes devem registrar origem e commit.
- Não versionar assets, binários, ISOs, mapas de funções, dumps ou screenshots. Capturas somente em `.playwright-mcp/`.
- Alterações no PS2Recomp precisam de patches reproduzíveis em `tools/patches/ps2recomp/`.
- devkitPro somente no Docker `docker-dev`; build não equivale a validação no hardware Switch.
- TDD nas mudanças de comportamento; sem declarar HD ativo por logs de inicialização ou contadores sem leitura de pixels.

## Levantamento em andamento

- Rastrear apresentação/contexto/thread, rasterização real e configuração GLES3.
- Identificar a fronteira de desenho com identidade de recursos, antes dos pacotes GS perderem essa informação.
- Conferir inventário HD disponível, paletas, layout, texto e transformação inversa de entrada.
- Definir tarefas e verificação a partir das interfaces realmente existentes.


# Plano: Renderização Nativa em 1080p (GLES3/Raylib) com Assets HD Edition

## Contexto

Atualmente, ao executar a primeira missão da campanha (William Wallace) em 1920×1080, o jogo abre na resolução especificada, porém a interface e o jogo são apresentados por meio de upscale bilinear do framebuffer PS2 de baixa resolução (640×448). No módulo GPU nativo (`game_hooks/src/kcet_gpu_module.cpp`), o `NativeDrawHook` retorna `false` incondicionalmente e a renderização em `GlesRenderer` ainda não desenha quads nem rasteriza fontes em alta resolução no frame apresentado.

**Objetivo:** Substituir a apresentação esticada de baixa resolução por uma renderização nativa em alta definição (1920×1080 no desktop / docked e 1280×720 no Switch), utilizando os assets HD (paletas e sprites SLP 2.0N da edição HD 2013), layout widescreen recomposto via `HudLayout` e mapeamento de cliques/toque com `HudInverseMapper`, mantendo a simulação do jogo intacta e com fallback seguro.

---

## Arquitetura e Decisões de Design

1. **Pipeline de Apresentação e Contexto Gráfico:**
   - O PS2Recomp gerencia a janela e o contexto gráfico via Raylib (`ps2xRuntime/src/lib/ps2_runtime.cpp`).
   - Em vez de tentar criar um segundo contexto EGL conflitante no mesmo processo, o `GlesRenderer` / camada nativa injeta o desenho dos elementos de interface HD e renderização nativa diretamente no loop de desenho do host (`BeginDrawing()` / `EndDrawing()`), ou via shaders/quads OpenGL ES 3.0 / GL 3.3 compatíveis.
   - O framebuffer PS2 do mundo continua sendo renderizado pelo backend do jogo, enquanto a interface (HUD superior, painel de comando inferior, minimapa, textos e cursores) é recomposta de forma nítida e nativa em 1080p.

2. **Recomposição da HUD Widescreen (1080p / 720p):**
   - Reutilização de `aoe2::renderer::HudLayout` (`renderer/src/hud_layout.cpp`):
     - Barra de recursos superior: ancorada ao topo, estendida ou preenchida com o asset HD `50792.slp` em 1080p.
     - Barra inferior de comando/minimapa: ancorada na base, utilizando os módulos `50150.slp`, `50100.slp` e `50341.slp`.
   - Reutilização de `aoe2::input::HudInverseMapper` (`input/src/hud_inverse_mapper.cpp`):
     - Mapeia as coordenadas de clique/toque de 1920×1080 na HUD realocada de volta para os retângulos de teste de colisão do guest (640×448), permitindo clicar em botões, unidades e comandos sem desalinhamento.

3. **Resolução de Assets HD e Cache:**
   - Correção do `aoe2::assets::AssetResolver`:
     - Chave de cache composta (`category`, `asset_id`, `frame_index`, `player_id`, `palette_id`) evitando que mudanças de paleta reutilizem texturas incorretas.
     - Suporte a decodificação com paleta `50507.bina` / `50500.bina` em conjunto com `SlpDecoder` e upload direto para texturas GPU.
     - Fallback gracioso: quando um asset HD não estiver mapeado ou disponível, preserva o desenho original do guest.

4. **Rasterização de Textos e Fontes Nativas:**
   - Renderização dos textos de recursos (Madeira, Alimento, Ouro, Pedra, População) e objetivos da missão em alta resolução nativa, sem o borrado do estiramento bilinear.

---

## Tarefas de Implementação

### Tarefa 1: Refatoração do `AssetResolver` e Cache de Texturas HD

**Arquivos:**
- `assets/include/aoe2/assets/asset_resolver.hpp`
- `assets/src/asset_resolver.cpp`
- `tests/cpp/asset_resolver_tests.cpp`

- [ ] **Step 1 (TDD Red):** Escrever teste unitário verificando que a chave de cache invalida ou distingue paletas diferentes e que a resolução de IDs mapeados retorna o sprite com dimensões e pixels corretos.
- [ ] **Step 2:** Executar `ctest -R asset_resolver` e verificar falha.
- [ ] **Step 3 (Green):** Implementar chave de cache estendida (`palette_id`) e registro de tabela de correspondência PS2 → HD (`RegisterMapping`).
- [ ] **Step 4:** Executar `ctest -R asset_resolver` e verificar aprovação.
- [ ] **Step 5:** Commit.

---

### Tarefa 2: Conexão do `GlesRenderer` ao Pipeline de Apresentação Nativa

**Arquivos:**
- `renderer/include/aoe2/renderer/gles_renderer.hpp`
- `renderer/src/gles_renderer.cpp`
- `renderer/include/aoe2/renderer/renderer.hpp`
- `tests/cpp/gles_renderer_tests.cpp`
- `CMakeLists.txt`

- [ ] **Step 1 (TDD Red):** Escrever teste para `GlesRenderer::DrawSprite` e upload de texturas garantindo que quads texturizados gerem comandos e coordenadas de projeção corretas para a resolução de destino (1920×1080).
- [ ] **Step 2:** Executar `ctest -R gles_renderer` e verificar falha.
- [ ] **Step 3 (Green):** Implementar geração e submissão de quads com coordenadas de tela normalizadas/projeção ortográfica para resolução nativa, compatível com o contexto Raylib/OpenGL.
- [ ] **Step 4:** Executar `ctest -R gles_renderer` e verificar aprovação.
- [ ] **Step 5:** Commit.

---

### Tarefa 3: Integração do `HudLayout` e `HudInverseMapper` no Módulo Gráfico e de Entrada

**Arquivos:**
- `game_hooks/src/kcet_gpu_module.cpp`
- `game_hooks/src/kcet_mouse_overrides.cpp`
- `input/include/aoe2/input/hud_inverse_mapper.hpp`
- `renderer/include/aoe2/renderer/hud_layout.hpp`
- `tests/cpp/hud_layout_tests.cpp`

- [ ] **Step 1 (TDD Red):** Escrever teste integrando `HudLayout` em 1920×1080 com `HudInverseMapper`, garantindo que coordenadas de cliques nas barras superior e inferior mapeiem com precisão para os limites 640×448 do guest.
- [ ] **Step 2:** Executar `ctest -R hud_layout` e verificar validações.
- [ ] **Step 3 (Green):** Configurar o layout de HUD para resolução de 1080p (`HudRegionRole::ResourceBar`, `HudRegionRole::CommandBar`) em `kcet_gpu_module.cpp` e integrar a transformação inversa no tratamento de cliques do mouse em `kcet_mouse_overrides.cpp`.
- [ ] **Step 4:** Executar `ctest -R hud_layout` e verificar sucesso.
- [ ] **Step 5:** Commit.

---

### Tarefa 4: Hook de Apresentação no Runtime (Patch PS2Recomp)

**Arquivos:**
- `third_party/PS2Recomp/ps2xRuntime/include/ps2_runtime.h`
- `third_party/PS2Recomp/ps2xRuntime/src/lib/ps2_runtime.cpp`
- `tools/patches/ps2recomp/0008-native-hud-presentation-hook.patch`

- [ ] **Step 1 (Red):** Criar teste/verificação de callback de apresentação do host no runtime para desenho sobreposto antes do swap de buffers.
- [ ] **Step 2 (Green):** Adicionar hook de renderização customizada pós-frame no `ps2_runtime.cpp` (`m_customRenderCallback`), permitindo que a camada nativa desenhe elementos em resolução total (1920×1080) sobre a janela do Raylib.
- [ ] **Step 3:** Gerar patch rastreado `tools/patches/ps2recomp/0008-native-hud-presentation-hook.patch` e validar via `python3 -m unittest tests.test_runtime_patches`.
- [ ] **Step 4:** Commit.

---

### Tarefa 5: Mapeamento de Assets HD da Interface e Renderização da HUD em 1080p

**Arquivos:**
- `game_hooks/src/kcet_gpu_module.cpp`
- `game_hooks/src/gpu_runtime_adapter.hpp`
- `assets/src/asset_resolver.cpp`

- [ ] **Step 1 (TDD Red):** Escrever teste de renderização da HUD com `AssetResolver` carregando os SLPs `50792` (barra superior), `50150` (barra inferior) e paleta `50507`.
- [ ] **Step 2 (Green):** Conectar os sprites decodificados ao callback de apresentação nativa, desenhando a barra de recursos e o painel de comando nas posições 1080p do `HudLayout`.
- [ ] **Step 3:** Executar `ctest` da suíte de renderização e asset resolution.
- [ ] **Step 4:** Commit.

---

### Tarefa 6: Validação End-to-End na Primeira Missão (William Wallace) em 1920×1080

**Arquivos:**
- `tools/drive_desktop_scenario.py`
- `tools/validate_desktop.py`

- [ ] **Step 1:** Executar build do runner diagnóstico com Docker:
  `python3 tools/docker_build.py --platform desktop --diagnostics --jobs 4`
- [ ] **Step 2:** Executar cenário de campanha em 1080p:
  `python3 tools/drive_desktop_scenario.py campaign --resolution 1920x1080`
- [ ] **Step 3:** Inspecionar a captura `.playwright-mcp/campaign-mission.png` para confirmar que a barra de recursos superior e os painéis de comando utilizam os sprites HD nítidos em vez da textura borrada esticada.
- [ ] **Step 4:** Verificar que o soldado escocês responde ao clique/seleção e que os cliques na HUD funcionam com o mapeamento inverso.
- [ ] **Step 5:** Commit final da documentação e resultados.

---

## Verificação e Critérios de Sucesso

1. **Testes Automatizados:**
   - Todos os testes C++ (`ctest --test-dir build`) passando, cobrindo `asset_resolver`, `slp_decoder`, `jasc_palette`, `gles_renderer`, `hud_layout` e `runtime_patches`.
2. **Qualidade Visual em 1080p:**
   - A captura de tela `campaign-mission.png` em 1920×1080 exibe bordas e elementos da HUD nítidos (1:1 com os assets do HD Edition), sem estiramento borrado 4:3.
3. **Interatividade e Jogabilidade:**
   - O mouse em 1080p seleciona e move unidades corretamente no mapa de William Wallace graças ao `HudInverseMapper`.
4. **Compatibilidade Switch:**
   - O código utiliza tipos e interfaces portáteis (`HudLayout`, `GlesRenderer`, `AssetResolver`) compatíveis com 1280×720 handheld e 1920×1080 docked.
