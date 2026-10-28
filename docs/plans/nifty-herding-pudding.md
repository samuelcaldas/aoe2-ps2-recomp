# Plano: apresentação HD/GLES3 1080p — geometria correta, captura semântica e assets HD

## Contexto

Pedido: "fix hd/gles 1080p". Estado verificado em 2026-10-27 (commit `324ab1d`):

- A janela abre em 1920×1080, mas mostra o framebuffer GS 640×512 ampliado com
  filtro bilinear (`ps2_runtime.cpp` run loop, `DrawTexturePro` com aspect-fit).
  Na captura `.playwright-mcp/campaign-mission.png` a área útil tem ~1350×1080
  (proporção 640:512), e o HUD ocupa as linhas ~2–505 de 512.
- Mouse (`kcet_mouse_overrides.cpp`, `SurfaceMapping` 640×448) e o driver de
  cenário (`tools/drive_desktop_scenario.py`, aspect-fit 640×448) usam outra
  transformação. Em 1080p, isso dá ~1543 px de largura contra os 1350 px exibidos:
  o erro é horizontal **e** vertical. Os clamps do cursor também divergem. O comentário
  do hook cita `0x2257C0`, com y em 0..431. O decomp tem `func_002255A0`, com y em 0..479.
- O overlay HD anterior foi revertido. `GlesRenderer` só grava comandos. Não cria
  contexto nem desenha quads. O build desktop usa `GRAPHICS_API_OPENGL_33`.
- Não existe mapeamento PS2→HD. O disco PS2 guarda os dados em `DATA.CAT`/`ALL_DATA.BIN`
  (formato não verificado), e não há `.drs`. O HD 2013 local tem SLPs soltos por ID
  (170 em interface, 1768 em graphics), 32 paletas JASC e 18 TTFs.
- No decomp recuperado, o único ponto de desenho concreto é `func_0021C990`. Ele monta
  quad texturizado a partir de UV e pontos e não recebe ID de recurso. O jogo carrega
  shapes por nome/ID via `func_25D6D0` (ex.: `"%s.slp"`, id `0x124E`) e tem um
  decodificador SLP 2.0N próprio.
- Os dumps antigos do GS (`build/desktop-validation/gs-*.ppm`, de menus) mostram três
  texturas: um "back buffer" **T8 640×448** que cobre 99% da tela, um atlas de fonte
  **T4 1024×256** e uma imagem T8 128×128. Hipótese: o jogo compõe a tela lógica
  640×448 por software num buffer de 8 bits, como o motor PC. O GS então desenha essa
  textura esticada para 640×512 (PAL), e o texto vai em quads separados. Ainda não
  foi verificado durante o gameplay.

Resultado pretendido: a primeira missão (William Wallace) apresentada em 1920×1080
(e em 1280×720), com geometria e mouse corretos e cena redesenhada em resolução nativa.
Os sprites/HUD trocados por SLPs do HD Edition só entram onde o mapeamento for
verificado. O fallback continua sendo o framebuffer guest, nunca overlay decorativo.

## Restrições

- Trabalhar em `main`, sem worktrees e sem PRs; commit por tarefa.
- Não editar C++ gerado; não alterar simulação/regras/entrada para mascarar gráfico.
- Mudanças no PS2Recomp só como patch em `tools/patches/ps2recomp/NNNN-*.patch`
  (próximo número: `0009`), verificadas por `tests/test_runtime_patches.py`.
- Nada de assets, dumps, tabelas de correlação, endereços derivados do binário ou
  screenshots versionados. Tabelas locais ficam em `game/SLES-50282/` (ignorado);
  capturas em `.playwright-mcp/`. Endereços/nomes do decomp `../aoe` podem ser
  citados, com origem e commit.
- devkitPro só no Docker `docker-dev`; build Switch não é validação de hardware.
- TDD em toda mudança de comportamento; "HD ativo" só com leitura de pixels.
- Cada hook replica o comportamento original (chama o original) até a paridade 1×
  ser provada. O framebuffer guest é sempre o oráculo e o fallback.

## Estado e redirecionamento (2026-10-28)

- Fase 0 concluída (`ce7ed67`, `4cddf2d`, `36c8784`; `docs/hd-render-evidence.md`).
  A composição é híbrida: o playfield 640×262 é composto por software em RAM
  `0x01F98C80` por rasterizador de spans. Texto e HUD vêm em uploads por elemento.
  A tela lógica 640×448 é esticada no frame PAL 640×512.
- Fase 1 concluída: transformação única e calibração em dois pontos com erro (0,0).
- O ponto de corte semântico foi achado: `func_0025E640(shape, area, x, y, frame,
  flags)`, com o loader `func_0025D6D0(shape, nome, id)`. Os IDs do PS2 seguem a
  numeração do PC.
- Os assets do HD Edition 2013 são os SLPs do PC na resolução original. Trocar
  sprites com a vista mantida em 640×448 não dá detalhe de 1080p.
- **Decisão do usuário (2026-10-28): vista ampliada tipo PC.** O mapa passa a ser
  renderizado na resolução da janela, com sprites 1:1, e o HUD é recomposto. Essa
  meta substitui as Fases 2–4 abaixo pelas Fases A–D:

### Fase A — Viewport do motor configurável
Localizar onde o motor define a área do mapa (largura/altura da vista 640×262 em
y=40, o retângulo de clip do rasterizador `D_004E30DC..E8` no decomp e o buffer
`0x01F98C80`). Fazer o motor compor uma vista maior num buffer guest maior. A
simulação não muda: só a área visível. Oráculo: com o tamanho original, o frame deve
ser idêntico ao atual.

### Fase B — Apresentação direta do playfield
O host converte o buffer de 8 bits do playfield, com a paleta ativa, para RGBA e o
apresenta 1:1 na janela. Esse caminho contorna o limite de 1024 px de textura e os
4 MB de VRAM do GS. O HUD e o texto do frame guest continuam até a Fase C.

### Fase C — HUD recomposto
Capturar os desenhos de HUD no ponto de corte de shapes e de texto e desenhá-los
ancorados às bordas da janela. Usar os SLPs de interface do HD quando o ID existir e
a escala for isotrópica. A entrada usa a mesma transformação por região.

### Fase D — Texto TTF, GLES3, 720p e Switch
Como na Fase 5 original.

## Fase 0 — Evidência (diagnóstico, sem mudança de comportamento)

Objetivo: decidir o ponto de captura e a geometria com dados de gameplay.

1. **Registradores de apresentação.** No build de diagnóstico, registrar por frame
   PMODE, SMODE2, DISPFB1/2 e DISPLAY1/2 crus. Usar o ponto onde
   `GS::buildPresentationRequestUnlocked` (`gs_frontend.cpp:506-524`) já copia esses
   valores, com uma variável de ambiente gate no patch `0009-presentation-diagnostics`.
   Registrar também DX/DY/MAGV, que `decodeDisplaySize` (`gs_cpu_backend.cpp:304-334`)
   hoje ignora.
2. **Modelo de composição.** Fazer um histograma por frame, em menu, briefing e missão,
   de uploads (BITBLT/TRXDIR: tamanho, PSM, destino) e de primitivas (tipo, TEX0,
   tamanho do quad). Partir do log de primitivas do patch `0002`. Responder: no
   gameplay, a cena é um T8 640×448 por frame (composição por software) ou muitas
   texturas por asset?
3. **Escritores do back buffer.** Se a composição for por software, usar o write-watch
   (patch `0004`) e `aoe2_call_trace.cpp` sobre o buffer de 8 bits. O objetivo é listar
   as funções que escrevem nele: blitter de shape SLP, preenchimento, terreno, linhas.
   Cruzar com o decomp (`func_25D6D0` e o tipo do shape carregado) para achar a assinatura
   do desenho de shape: ponteiro do shape, frame, x/y, cor do jogador, flags e clip.
4. **Clamp do cursor.** Confirmar no código gerado qual rotina limita o cursor que
   `SteerGuestCursor` move: `0x2257C0` (0..431) ou `func_002255A0` (0..479).

Saída: `docs/hd-render-evidence.md`, com valores de registradores, o modelo de
composição e o ponto de captura escolhido. Endereços citados só se vierem do decomp;
o resto fica no INI local ignorado (padrão de `tools/resolve_kcet_hooks.py`).

**Ponto de decisão:** se o gameplay usar texturas por asset, sem back buffer por
software, a única identidade disponível é o hash de textura no nível kt_gpu/GS. Isso
desvia do mandato "renderizar no alto nível" do CLAUDE.md. Nesse caso, parar e pedir
decisão ao usuário antes da Fase 2.

## Fase 1 — Transformação de apresentação única (independente de HD)

Objetivo: desenho, mouse e driver de cenário consomem a mesma transformação
janela ↔ frame ↔ tela lógica, redimensionável e correta em 1080p e 720p.

- Criar `aoe2::presentation::PresentationTransform` em `core/` (header + `.cpp`, sem
  dependência de SO). Entradas: tamanho da janela, tamanho do frame GS e retângulo
  lógico ativo dentro do frame. Pela hipótese da Fase 0, o retângulo é 640×448
  esticado para 640×512, a confirmar pelos registradores. Saídas: o retângulo de destino
  do `DrawTexturePro`, `ToLogical(window_xy)` e `ToWindow(logical_xy)`. Reusar a
  semântica de clamp de `input/src/surface_mapping.cpp`.
- Patch `0010-shared-presentation-rect`: `ps2_runtime.cpp` passa a publicar, por
  frame, o tamanho do frame latched e o retângulo de destino usado (atômico/mutex,
  thread do host). Opcionalmente aceita um provedor de retângulo instalado pelo host.
- `kcet_mouse_overrides.cpp::SteerGuestCursor` passa a usar a transformação publicada
  no lugar do `SurfaceMapping` fixo 640×448, com os limites confirmados na Fase 0.4.
- `tools/drive_desktop_scenario.py`: mesma fórmula, com constantes compartilhadas.
  Um teste Python compara seus pontos com os vetores do teste C++.
- Testes (RED primeiro): `tests/cpp/presentation_transform_tests.cpp`, com vetores em
  1920×1080, 1280×720, 640×448 e janela redimensionada não-16:9. Mais um teste de
  calibração em execução real, com dois pontos. Para cada ponto: posicionar o ponteiro
  do host, ler o cursor guest na RAM e localizar o sprite do cursor na captura; o
  erro tem de ficar ≤ 1 px lógico.

## Fase 2 — Captura da lista de desenho com oráculo 1×

Assumindo composição por software (Fase 0). Caso contrário, parar no ponto de decisão.

- Hooks de passagem (ADR 0001) nos escritores identificados: desenho de shape,
  preenchimentos, terreno e texto (ponto do atlas T4). Cada hook grava um comando
  semântico e depois chama o original. Comandos: `ShapeDraw{shape_key, frame, x, y,
  player, flags, clip}`, `Fill`, `TerrainTile`, `Text`. O `shape_key` sai do nome/ID
  passado a `func_25D6D0` no carregamento: um mapa ponteiro→chave preenchido por hook
  no loader. Nada de ler ponteiros guest sem sincronização.
- Lista por frame publicada no mesmo latch de VSYNC que `UploadFrame` já usa
  (`ps2_runtime.cpp:377-387`): buffer duplo, posse transferida para a thread do host.
- Oráculo: um replayer de CPU desenha a lista em 1× num buffer 8-bit, decodificando os
  SLPs do PS2 já carregados na RAM (reusar `assets/src/slp_decoder.cpp` se o formato
  coincidir). O resultado deve ser idêntico pixel a pixel ao back buffer guest do mesmo
  frame. Métrica de cobertura: % de pixels iguais por cena (menu, briefing, missão).
- Testes: `tests/cpp/draw_list_tests.cpp`, para ordem, buffer duplo e clip. Teste de
  paridade sobre um frame capturado localmente (fixture ignorada; pula com motivo
  explícito quando ausente, mais um fixture sintético rastreável que sempre roda).

## Fase 3 — Replay nativo em resolução da janela

- `GlesRenderer` vira backend real sobre `rlgl`/raylib, na thread que possui o
  contexto. Faz upload de textura, quads texturizados com projeção ortográfica e paleta
  via shader ou pré-decodificação RGBA. `IRenderer` recebe os comandos da Fase 2.
- A apresentação troca para o replay **por frame**, só quando a paridade 1× do frame for
  100%. Nos demais frames, usa o framebuffer guest. A escala vem da
  `PresentationTransform` da Fase 1, mantendo a proporção da tela lógica com
  pillarbox: sem widescreen nesta fase.
- Teste: renderização offscreen (`LoadRenderTexture` + `LoadImageFromTexture`) de uma
  lista sintética em 1920×1080. Comparar com a referência gerada pelo replayer CPU
  escalado (nearest). Diferença 0 nas bordas dos quads.

## Fase 4 — Substituição por assets HD Edition

- Ferramenta local `tools/correlate_hd_shapes.py` (stdlib). Para cada `shape_key`
  visto em gameplay, decodifica o SLP PS2 e os candidatos HD (`LooseHdAssetProvider`,
  `AssetResolver`, `JascPalette`). Compara frame a frame, com contagem de frames,
  dimensões ×2 e similaridade após redução. Escreve `game/SLES-50282/hd_shape_map.json`
  (ignorado) com score por entrada. Uma entrada só fica ativa após inspeção visual
  registrada.
- O replay usa o frame HD com o mesmo hotspot/âncora (escala HD/PS2 por eixo **igual**;
  rejeita mapeamentos anisotrópicos) e a paleta HD verificada. Sem mapeamento, desenha o
  shape PS2. Ordem: peças estáticas do HUD, depois unidades/edifícios e por último
  terreno.
- Testes: `asset_resolver_tests` / `slp_decoder_tests` para âncora e escala isotrópica.
  Teste Python da ferramenta com fixtures sintéticos.

## Fase 5 — Texto nativo e GLES3 real

- Texto: no ponto de string da Fase 2, rasterizar com TTF do HD (raylib `LoadFontEx`)
  no tamanho de destino. Fallback para os glyphs do atlas T4.
- GLES3: build desktop com `GRAPHICS_API_OPENGL_ES3` (raylib via ANGLE/Mesa EGL no
  Docker desktop). Afirmar `GL_VERSION`/`GL_SHADING_LANGUAGE_VERSION` em runtime e no
  log. Build Switch no `docker-dev`, sem afirmar validação de hardware.

## Arquivos críticos

- `third_party/PS2Recomp/ps2xRuntime/src/lib/ps2_runtime.cpp`,
  `.../gs/gs_cpu_backend.cpp`, `.../gs/gs_frontend.cpp`: só via patches `0009`+.
- `core/` (nova `PresentationTransform`), `input/src/surface_mapping.cpp` (reuso).
- `game_hooks/src/kcet_mouse_overrides.cpp`, `game_hooks/src/kcet_gpu_module.cpp`,
  `game_hooks/src/aoe2_call_trace.cpp`, novos hooks de captura em `game_hooks/src/`.
- `renderer/src/gles_renderer.cpp`, `renderer/include/aoe2/renderer/renderer.hpp`.
- `assets/src/{slp_decoder,asset_resolver,loose_hd_asset_provider,jasc_palette}.cpp`.
- `tools/drive_desktop_scenario.py`, `tools/correlate_hd_shapes.py` (novo).
- `docs/hd-presentation-validation.md` (gates), `docs/hd-render-evidence.md` (novo),
  `README.md` (status).

## Verificação

- Por tarefa: `python3 -m unittest discover -s tests`; `cmake --build build && ctest
  --test-dir build --output-on-failure`. Os novos testes devem ser vistos falhando antes.
- Patches: `python3 -m unittest tests.test_runtime_patches`, com toda a série aplicável
  e já aplicada.
- Build: `python3 tools/docker_build.py --platform desktop --diagnostics --jobs 3`.
- Execução real: `python3 tools/drive_desktop_scenario.py campaign --resolution
  1920x1080 --keep-card` e o mesmo em `1280x720`. Ler as capturas de menu, briefing,
  missão e retorno ao menu em `.playwright-mcp/`. Conferir cliques em unidade, movimento,
  botões de comando e minimapa com a calibração da Fase 1.
- Critério final: na missão, os pixels apresentados vêm do replay nativo (log por frame
  de origem replay/fallback e taxa de paridade), com elementos HD mapeados nítidos em
  1080p. Nenhum conteúdo dinâmico pode sumir em relação ao frame guest.
- Cada fase atualiza `docs/hd-presentation-validation.md` só com o que foi medido.
