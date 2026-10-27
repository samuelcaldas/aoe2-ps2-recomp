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
