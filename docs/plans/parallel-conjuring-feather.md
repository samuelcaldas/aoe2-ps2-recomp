# Plano: integrar assets HD e HUD widescreen ao fluxo de build (TDD)

## Contexto

A extração dos assets HD (v5.8, 354 itens de `interface`) já está na `main`: `tools/import_hd_assets.py`, `catalog.json`, `hd_assets_manifest.json`, `docs/hd-assets.md`. O que falta é usá-los:

1. **Não existe etapa de preparação de dados no build.** Os dados do PS2 chegam ao destino por `adb push` dentro de `tools/validate_android.py`, por leitura direta de `game/SLES-50282/` no desktop e por cópia manual para `sdmc:/switch/aoe2/` no Switch. Nada prepara os assets HD.
2. **Não existe caminho de assets HD até a tela.** O jogo desenha com um GS em software dentro do runtime upstream (`third_party/PS2Recomp/ps2xRuntime`, `GSCpuBackend`) e apresenta via raylib em 640x448. `IRenderer` não está ligado ao runtime. O hook `kt_gpu` do ADR 0001 não foi iniciado. Não há decodificador de SLP nem de paleta, nem correlação PS2 → HD, nem camada de layout de HUD.

**Decisão do usuário:** ligar ao jogo agora, com a parte que toca o runtime entregue ao agente `@aoe-recomp`. Eu entrego as interfaces, as peças testáveis e a especificação de hand-off.

**Restrição:** `@aoe-recomp` tem prioridade. A árvore principal tem trabalho dele não commitado em `CMakeLists.txt`, `cmake/Aoe2Recompiled.cmake`, `tools/docker_build.py`, `tools/validate_*.py`, `game_hooks/` e `input/`. Este plano só **cria arquivos novos**, numa worktree própria. As poucas edições em arquivos dele ficam em uma etapa final, só depois de a árvore dele estar limpa. Nenhum arquivo é apagado ou movido.

## Divisão do trabalho

| Trilha | Dono | Conteúdo |
|---|---|---|
| A. Ferramentas e dados | eu | decodificador SLP + paleta, conversão para PNG, preparação por plataforma, manifesto |
| B. Layout do HUD | eu | modelo de HUD widescreen e transformação inversa de entrada, em C++ testável sem runtime |
| C. Ligação ao runtime | `@aoe-recomp` | hook de desenho, correlação PS2 → HD, constantes de viewport, apresentador widescreen |

A trilha C só começa quando A e B tiverem contratos estáveis e testes verdes.

## Trilha A: ferramentas e dados (arquivos novos)

Python stdlib, falha cedo, no padrão de `tools/hd_assets/`. Cada módulo nasce de um teste que falha.

- `tools/hd_assets/slp_decoder.py`: decodifica SLP 2.0N (tabela de contornos, offsets de comandos, comandos de desenho) para índices de paleta. A especificação é a de `doc/media/slp-files.md` do openage; o código é escrito do zero (GPL-3.0, sem copiar).
- `tools/hd_assets/palette.py`: lê a paleta. O formato dos `.bina` de paleta da HD **não foi verificado**. O primeiro teste usa um arquivo real (`AOE2_HD_SOURCE`) para descobri-lo antes de escrever o parser.
- `tools/hd_assets/png_writer.py`: gera PNG RGBA só com `zlib` e `struct`. A escolha de PNG é porque o raylib do runtime já carrega PNG nas três plataformas.
- `tools/hd_assets/stage_plan.py`: função pura `plan_stage(catalog, platform) -> list[StageEntry]` com origem, destino relativo e digest. Raízes de destino por plataforma, todas relativas ao diretório do ELF: desktop `game/SLES-50282/hd/`, Android `$DEVICE_GAME_DIR/hd/`, Switch `sdmc:/switch/aoe2/hd/`.
- `tools/stage_hd_assets.py --platform {desktop,android,switch} [--dry-run]`: converte SLP em PNG, grava um `hd_stage.json` local (digests e tamanhos) e, no desktop, copia para a pasta de dados. Android e Switch só geram a árvore pronta em `build/hd-stage/<platform>/`, que a etapa de entrega copia depois.
- `tools/verify_hd_stage.py`: reconfere a árvore preparada contra `hd_stage.json`; falha em qualquer divergência.
- `hd_stage_manifest.json` rastreável: só contagens e digests agregados, sem nomes nem ids. `verify_hd_stage.py` rejeita chaves ou valores com barra, como o verificador atual.

Testes (`tests/test_hd_stage.py`, `tests/test_slp_decoder.py`): SLP sintético de 1 quadro e de vários, quadro truncado, offset fora do limite, paleta curta, PNG relido byte a byte, `plan_stage` com 0/1/muitos itens, plataforma desconhecida, reexecução idempotente, árvore adulterada detectada. Os testes com instalação real só rodam com `AOE2_HD_SOURCE`.

## Trilha B: layout do HUD (C++, arquivos novos)

Em `renderer/` e `input/`, namespaces `aoe2::renderer` e `aoe2::input`, sem dependência do runtime.

- `renderer/include/aoe2/renderer/hud_layout.hpp` e `renderer/src/hud_layout.cpp`: `HudRegion` (retângulo original em pixels de guest, âncora, retângulo de destino), `HudLayout::ForExtent(Extent screen)` que ancora painéis às bordas e declara os preenchimentos (barras `50792`, `50150`, painel lateral `50341`) por **papel**, não por id. O mapeamento papel → id da HD vem da trilha C.
- `input/include/aoe2/input/hud_inverse_mapper.hpp` e `input/src/hud_inverse_mapper.cpp`: mapeia um ponto da tela para as coordenadas originais do painel, entre `SurfaceMapping` e `GuestMouseAdapter`, como pede `architecture.md` §7.4. Cada região realocada tem entrada no layout de desenho e no de entrada, e um teste garante que as duas listas coincidem.
- `tests/cpp/hud_layout_tests.cpp`, com o macro `CHECK` de `tests/cpp/input_tests.cpp`. Casos: 4:3 reproduz o original, 16:9 (1280x720 e 1920x1080), extensão mínima, painéis sem sobreposição, ponto fora de qualquer região, ida e volta (desenho → entrada) com erro zero em cada vértice.
- Registro do teste: uma linha `add_executable` + `add_test` em `CMakeLists.txt`, no padrão das linhas 52-57. Fica na etapa final.

## Trilha C: ligação ao runtime (hand-off a `@aoe-recomp`)

Eu escrevo `docs/plans/hd-widescreen-handoff.md` com o contrato e envio ao agente por `SendMessage` depois da aprovação, sem tocar nos arquivos dele. Conteúdo do hand-off:

1. **Hook de desenho:** resolver as funções de `kt_gpu` pelo mecanismo existente (`tools/resolve_kcet_hooks.py` → `kcet_hooks.ini` → `HookHandlers()` em `game_hooks/`), com nova chave `kcet.gpu.<fn>`. Handlers no formato `void(uint8_t*, R5900Context*, PS2Runtime*)`.
2. **Correlação PS2 → HD:** tabela local derivada dos arquivos do usuário (nunca rastreada), com queda para o asset do PS2 sem correspondência verificada (§7.3). Os dados do disco já estão extraídos em `game/SLES-50282/` (`DATA.CAT`, `ALL_DATA.BIN`); o formato desse contêiner é **desconhecido** e precisa de um catálogo por assinatura, como foi feito para a HD.
3. **Viewport widescreen:** localizar as constantes de viewport, limite de rolagem, culling e clamp do cursor (§7.2) e sobrescrever por hook. Hoje a janela é 640x448 enquanto o guest assume 640x480; isso precisa ser reconciliado antes.
4. **Apresentador:** apresentar em extensão widescreen (ex.: 854x480) com `HudLayout::ForExtent` e `HudInverseMapper` no caminho do mouse. O caminho atual do mouse (`kcet_mouse_overrides.cpp`) acumula movimento relativo do raylib e **não** passa por `SurfaceMapping`; precisa ser ajustado.
5. **Entrega no build:** chamar `stage_hd_assets.py` em `docker_build.py` (nova fase por plataforma) e substituir o `adb push` de `validate_android.py` pela árvore preparada, para o Android e o Switch receberem `hd/` junto dos dados do PS2.

## Etapa final (só com a árvore do outro agente limpa)

Numa worktree: adicionar a linha de teste C++ em `CMakeLists.txt`, e o link para os novos documentos em `docs/architecture.md` §7.3/§7.4 e `docs/hd-assets.md`. Se a árvore ainda estiver suja, deixo essas linhas como patch documentado no hand-off, sem editar.

## Ordem (TDD, vermelho → verde → refatorar)

1. Worktree `.worktrees/hd-integration`, a partir de `main`.
2. Trilha A: palette (descoberta com dado real) → SLP → PNG → `plan_stage` → CLIs.
3. Trilha B: `HudLayout` → `HudInverseMapper` → teste de coerência desenho/entrada.
4. Hand-off e `SendMessage` ao agente.
5. Commits lógicos, merge `--no-ff` em `main`, testes em `main`, remover worktree e branch.

## Verificação

```bash
python3 -m unittest tests.test_slp_decoder tests.test_hd_stage
python3 -m unittest discover -s tests
AOE2_HD_SOURCE="<instalação HD>" python3 -m unittest tests.test_slp_decoder
python3 tools/stage_hd_assets.py --platform desktop --dry-run
python3 tools/stage_hd_assets.py --platform switch
python3 tools/verify_hd_stage.py --platform switch
python3 tools/docker_build.py --platform desktop --dry-run   # confirma que o fluxo existente segue igual
git check-ignore build/hd-stage game/SLES-50282/hd            # nada disso é rastreado
git diff --stat main...HEAD                                    # só arquivos novos
```

O teste C++ roda pelo fluxo Docker do desktop (`ctest -R hud_layout_tests`), depois que a linha for registrada.

Critérios de aceitação: a árvore preparada é idêntica em duas execuções; adulterar um byte falha a verificação; o manifesto rastreável não vaza nomes, ids nem caminhos; a ida e volta de coordenadas do HUD tem erro zero; nenhum arquivo do outro agente foi alterado antes da etapa final.

## Riscos e incógnitas

- **Não há ponto de interceptação de desenho hoje**, e o `kt_gpu` depende de casar a decompilação. A trilha C pode não caber numa iteração; o plano não promete HUD visível em tela, só o contrato e as peças prontas.
- **Correlação PS2 → HD não verificada.** A hipótese de ids coincidentes nunca foi testada, e o contêiner do disco PS2 é desconhecido.
- **Formato dos `.bina` de paleta da HD não verificado.**
- **O widescreen pode exigir mais que o HUD:** o playfield depende de constantes que ainda não foram localizadas (§7.2, §7.5).
- **Switch sem build validado:** o caminho `sdmc:/switch/aoe2/` é só intenção no CMake.
- **Licenças:** os assets HD e os PNG derivados ficam só em `game/` e `build/` (ignorados); o openage é GPL-3.0 e só serve de especificação.
