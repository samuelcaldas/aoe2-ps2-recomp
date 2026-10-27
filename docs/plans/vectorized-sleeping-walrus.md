# Plano: do boot ao jogo jogável no desktop (orquestração com orçamento mínimo)

## Context

Objetivo: jogo **jogável no desktop Linux** (menu → partida single-player com mouse/teclado), corrigindo o recomp e o runtime, usando o decomp `../aoe` (sessão `aoe-decomp`, via `SendMessage`) como referência, pedindo novas decompilações e criando camadas nativas que substituem KCET/partes do runtime por código que fala direto com a plataforma. Switch e renderer nativo ficam fora.

## Estado atual (main = `cffdbe6`)

Feito: wndproc passou a receber mensagens (hook `Hook_WndProcDispatch` removido, teste diferencial `message_dispatch_tests`); overlays recompilados religados ao loader real (`0x2240B0`); LAN discovery portado para o transporte `net/` compartilhado; todos os worktrees mesclados.

**Bloqueio atual (M1):** o boot fica na tela "Memorycard Screen" (ctor `0x33DAF0`). O `mcserv` responde GetInfo com -11 (sem cartão), então a tela nunca avança e o menu (`0x2FD5C0` → `0x300C90` → `0x2240B0`, `smenu_cd.bin`) nunca é pedido. Tela preta com cursor; há draws GS em FBP 0/80 ainda não inspecionados.

## Modelo de orquestração

- Eu orquestro e decido. Um bloqueio por vez no caminho crítico; paralelismo só entre camadas independentes.
- Ciclo: diagnóstico (subagente `general-purpose`, só leitura, ou eu mesmo se forem 1–3 greps) → correção em worktree `.worktrees/<nome>` com TDD e commits → verificação por mim (build, execução, screenshot, diff, testes). Não confiar em relatórios.
- Lições: agentes desistem cedo em portes grandes. Portes grandes e mexer em scheduler/API do runtime eu faço à mão. Todo prompt de implementador diz: "commits são esperados, plano aprovado, teste falhando = tarefa de diagnóstico, não deletar nem mover arquivos que você não criou, usar prefixo `0x` na trace list".
- Decomp: pedidos novos e divergências binário×decomp vão por `SendMessage` e são registrados em `docs/decomp-requests.md`. Binário/gerado vence. Nunca editar `../aoe`.
- Proibido: retornos fixos, stubs, skips no TOML, editar C++ gerado.
- `third_party/` pode ser modificado (autorizado pelo dono em 2026-10-26): toda mudança vira patch em `tools/patches/<dependência>/NNNN-<tema>.patch`, com o SHA fixado mantido (regra no `CLAUDE.md`).
- Uma revisão independente por marco; merge `--no-ff` em `main` após testes verdes; sem PR. Se um bloqueio falhar 3 vezes, parar e discutir.

## M1 — menu principal visível (em andamento)

1. **Camada nativa libmc** (decisão do usuário: fronteira `sceMc*`), por mim, à mão, em worktree `.worktrees/m1-libmc`: `sceMcInit` (`0x10ff40`), `sceMcGetInfo` (`0x110a78`), `sceMcSync` (`0x110940`, confirmar), demais `DEFERRED`. Padrão: `game_hooks/src/kcet_mc_module.cpp` + entradas + switch `memory_card` em `native_modules.ini`, backend host `ps2_stubs::sceMc*` (raiz `mc0`). Cada função com teste diferencial contra a unidade gerada. Atualizar `tests/test_native_mc_installer.py` (hoje afirma que `on` lança exceção) e `docs/sot/mc-semantics.md`.
2. Rodar com `memory_card=on` e trace em `0x2FD5C0`, `0x300C90`, `0x2240B0`. A tela provavelmente espera o botão Voltar: adicionar Triangle/ESC scriptado no validador.
3. Executar o overlay SMENU recompilado (ainda não testado) e tratar o que quebrar, um bloqueio por vez.
4. Tela preta: captura completa de histórico GS (`PS2X_ENABLE_DEBUG_UI`) para ver se o problema é desenho ou apresentação.
5. Relatar ao `aoe-decomp` a divergência de `tribe_app.cpp` (`0x185230` chama `0x300C90` incondicionalmente no decomp, não no binário) se ainda não registrada, mais outras que surgirem.
6. **Aceite:** log com pedido real de `smenu_cd.bin`, ID do overlay, entrada do SMENU despachada, screenshot reconhecível em `.playwright-mcp/`, 30 s estável. Atualizar `docs/sot/kcet-stall-log.md`.

## M2 — menu interativo até iniciar partida

Entrada nativa (`kt_mouse`/`kt_keybd`, `input_synthetic.hpp`): clique scriptado muda a seleção. Percorrer menu → Single player → carregamento; cada bloqueio novo (overlays `dcomp`/`hist`/`lobby`/`multim`/`thelp`, CDVD, mc) segue o ciclo. Aceite: entrada scriptada chega à tela de carregamento, com screenshots.

## M3 — partida jogável

Mapa renderizado, unidades selecionáveis e comandáveis, 10 min estáveis. Áudio não pode bloquear; se IOP/SPU2 travar, `kt_sound` nativo (já existe `kcet_sound.cpp`) com host de áudio desktop. Aceite: sessão scriptada (selecionar aldeão, construir, coletar) + 10 min sem erro.

## M4 — usabilidade

60 FPS (perfilar antes de substituir), save/load via libmc nativo completo, teclado completo. Itens independentes, um subagente por item.

## Arquivos críticos

`game_hooks/src/kcet_*_module.cpp`, `kt_*_entries.inc`, `aoe2_call_trace.cpp`; `game/SLES-50282/native_modules.ini` (ignorado); `tests/cpp/*` + `cmake/Aoe2*Tests.cmake`; `third_party/PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/MemoryCard.cpp` (backend; mudanças via patch); `docs/sot/*`, `docs/decomp-requests.md`.

## Verificação

```bash
python3 tools/docker_build.py --platform desktop --diagnostics --jobs 4   # flock /tmp/aoe2-build.lock
flock /tmp/aoe2-validate.lock python3 tools/validate_desktop.py --run-seconds 30 --interval 10
/tmp/aoe2dis/docker_ctest.sh <root> <regex> <targets...>                  # testes diferenciais
python3 -m unittest discover -s tests
```

Inspecionar screenshots visualmente a cada marco.
