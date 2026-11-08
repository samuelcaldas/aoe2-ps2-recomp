# Plan: cenário de validação "Single Player → Random Map → Start Game" (PC, Switch, Android)

## Context

Os testes automatizados de PC, Switch e Android validam hoje o jogo levando-o ao primeiro mapa da campanha
(`campaign` / `campaign-auto`). O objetivo novo é um único fluxo comum: **Single Player → Random Map → Start Game**,
com abordagem de controle específica por plataforma (mouse no PC, pad no Switch, toque no Android). Os cenários de
campanha são desativados e os novos são criados, com padrões de projeto para coesão e padronização.

Achados da exploração (resumo):
- PC: `tools/drive_desktop_scenario.py` (xdotool/Xvfb, coordenadas lógicas 640x448 via `tools/presentation_geometry.py`).
  Switch: `tools/drive_switch_scenario.py` (Ryujinx; pad = teclas xdotool; detecção de estado por numpy/PIL).
  Ambos são tabelas `(nome, ação, arg, settle)` com `do_step` em cadeia if/elif e funções duplicadas
  (`follow_guest_event`, `capture_screenshot`). Nenhuma classe compartilhada.
- Coordenadas conhecidas (640x448): Single Player (458,85); Random Map ≈ (458,124) (linha 2 do submenu, passo 38 =
  `CAMPAIGN_ROW_OFFSET`, hoje sem uso). **Start Game: desconhecido** (tela Game Settings nunca capturada).
- Switch: `validate_switch.evidence_passed` já retorna False para campanha (aceite visual); tabela e `do_step`
  divergem em coordenadas (wallace-marker, scenario-1).
- Android: `validate_android.py` só faz install + `am start` + `screencap` + logcat; **não há entrada** (NativeActivity
  `hasCode=false`, sem `AInputEvent`). Decisão do usuário: criar driver adb **e** ligar toque nativo.
- Decisão do usuário: aceite final "mapa visível" = screenshots por checkpoint + minha revisão visual; testes
  automáticos checam marcadores de log e frames.

## Design (padrões)

Novo pacote `tools/scenarios/` (stdlib, Python):
- **Command**: `Step` (dataclass imutável: `name`, `intent`, `settle_seconds`, `checkpoint: bool`) + ações concretas
  (`Tap(point)`, `Press(button)`, `Wait`, `AwaitMarker(marker, timeout)`).
- **Strategy**: `InputBackend` (protocolo) com `tap(point)`, `press(button)`, `settle()`:
  `XdotoolMouseBackend` (PC), `RyujinxPadBackend` (Switch, mapa botão→tecla já existente em
  `send_pad_button`), `AdbTouchBackend` (Android, `adb shell input tap` com conversão lógico→tela via
  `presentation_geometry.to_window`).
- **Template Method**: `ScenarioRunner.run()` = boot → perfil → executar steps → capturar checkpoint → avaliar
  marcador; plataformas só sobrescrevem `boot()` e fornecem backend.
- **Factory/Registry**: `scenario_for(platform)` devolve `RandomMapStartGame` com os steps do fluxo comum e
  as variações por plataforma (PC/Android clicam em coordenadas; Switch navega com D-pad + Cross, gated por estado).
- **Observer**: `CheckpointRecorder` (screenshots nomeados `<platform>-random-map-<step>.png` em
  `.playwright-mcp/<platform>/`) e `LogMarkerWatcher` (unifica `follow_guest_event` duplicado).
- Fluxo comum compartilhado em `tools/scenarios/random_map.py`: `main-menu → single-player → random-map →
  game-settings → start-game → loading → map`. Constantes de coordenadas num único módulo
  (`tools/scenarios/layout.py`), eliminando as divergências tabela×código.

## Steps

1. **Descobrir Start Game (captura + visão)**: rodar o build PC Release (`docker build` já existente), usar o driver
   atual até Single Player → Random Map, capturar a tela Game Settings, ler com visão a posição de Start Game e
   fixar em `layout.py`. Também confirmar (458,124) de Random Map clicando e capturando. Registrar no doc.
2. **Criar `tools/scenarios/`** (modelos, runner, backends, recorder, watcher) com testes unitários primeiro (TDD):
   `tests/test_scenarios_core.py` (steps, runner com backend falso, ordem dos checkpoints, timeout de marcador).
3. **PC**: refatorar `drive_desktop_scenario.py` para usar o runner; cenário `random-map-start`; **remover**
   `campaign`, `campaign-auto`, `calibration`(manter só se usado por doc; caso contrário desativar) das opções;
   marcador de sucesso: transição de carregamento/`Starting` do mapa ou `[map-submit]` conforme logs reais do
   Random Map (verificar em execução; ver `docs/sot/kcet-stall-log.md` sobre o travamento conhecido).
4. **Switch**: refatorar `drive_switch_scenario.py` e `validate_switch.py` (`--scenario random-map-start` default no
   lugar de `campaign-auto`; rejeitar nomes antigos com erro claro); navegação por pad (Down/Cross) reutilizando
   `detect_screen_state` e adicionando estados `random_map_settings`/`loading` se necessário.
5. **Android**: (a) `AdbTouchBackend` + cenário em `validate_android.py` (novo flag `--scenario`), screenshots por
   checkpoint, `adb logcat` como fonte do marcador; (b) **toque nativo**: tratar `AInputEvent`
   (AMOTION_EVENT) em `platform/android`, convertendo para `TouchPoint` e reaproveitando
   `input/touch_gestures` + `SurfaceMapping` (como `platform/switch/src/switch_input.cpp`), com teste C++ em `tests/cpp/`
   e atualização do `CMakeLists`/gradle. Requer investigação inicial do ciclo `android_main`/NativeActivity.
6. **Desativar testes antigos**: remover/reescrever em `tests/test_drive_*`, `tests/test_validate_switch.py`
   (`test_campaign_rejects_false_positive_if_map_not_reached` vira teste de rejeição do cenário antigo),
   `tests/test_validate_desktop.py` (nome de amostra), `tests/test_validation_build_types.py`; novos
   `tests/test_random_map_scenario.py` (steps idênticos entre plataformas exceto backend; coordenadas vindas só
   de `layout.py`; nomes antigos rejeitados).
7. **Docs**: atualizar `CLAUDE.md` (comando de exemplo `--scenario campaign-auto`), `.claude/skills/docker-validate/SKILL.md`
   (também corrigir `--timeout`→`--run-seconds`), `docs/native-map-view-checkpoint.md`, `docs/switch-manual-navigation-checkpoint.md`,
   `docs/README.md`, e criar `docs/validation-scenarios.md` descrevendo o fluxo e as abordagens por plataforma.
8. **Revisão visual (aceite)**: rodar cada plataforma, abrir os screenshots por checkpoint e confirmar com visão que
   cada tela esperada apareceu (Single Player, Random Map hover/clique, Game Settings, carregamento, mapa).
   Se o travamento do Random Map reaparecer, reportar em vez de marcar como passado.

## Files

- Novos: `tools/scenarios/{__init__,steps,runner,backends,recorder,layout,random_map}.py`,
  `tests/test_scenarios_core.py`, `tests/test_random_map_scenario.py`, `docs/validation-scenarios.md`.
- Modificados: `tools/drive_desktop_scenario.py`, `tools/drive_switch_scenario.py`, `tools/validate_switch.py`,
  `tools/validate_android.py`, `tools/view_android_novnc.py` (se compartilhar boot), `platform/android/*`,
  testes citados, docs citados.
- Reutilizar: `tools/presentation_geometry.py` (`to_window`), `tools/frame_analysis.py`, `tools/classify_diagnostics.py`,
  `follow_guest_event`/`send_pad_button`/`detect_screen_state` (mover para o pacote), `input/touch_gestures`.

## Verification

- `python3 -m unittest discover -s tests` (Python); `ctest --test-dir build --output-on-failure -V` para o teste C++ de toque.
- `python3 tools/validate_switch.py --mode native --scenario random-map-start --dry-run` e execução real (Ryujinx).
- `python3 tools/drive_desktop_scenario.py random-map-start` (PC Release) e `python3 tools/validate_android.py --scenario random-map-start`.
- Revisão visual manual dos screenshots por checkpoint de cada plataforma.

## Riscos / pontos em aberto

- Travamento atual ao clicar Start Game (investigação separada em andamento) pode fazer o passo final falhar até ser corrigido.
- Toque nativo no Android é a maior parte do esforço; pode exigir iteração com o emulador (KVM necessário).
- Coordenada de Start Game e de Random Map só serão confirmadas no passo 1.
