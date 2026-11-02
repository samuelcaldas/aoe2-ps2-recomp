# Plano ativo: build funcional para Nintendo Switch

## Estado atual

- Implementado: flags de viewport e gráficos nativos desligadas por padrão, gating das fontes/hooks, caminho GS no runtime Switch e flags acessíveis no wrapper Docker.
- Implementado: reserva Map Arena desligada sem viewport; mudanças de resolução handheld/docked alimentam o mapeamento absoluto do ponteiro.
- Validado: 403 testes Python executados (4 ignorados); 52 testes C++ passaram sob Xvfb; manifesto de geração e pin de dependências passaram na verificação.
- Em andamento: build NRO Debug Switch com flags OFF. Ainda pendentes: inspeção Ryujinx, primeiro mapa, gameplay, tutorial, save/load e console físico.

Este documento registra a mudança de prioridade do projeto. O objetivo imediato é iniciar e jogar AoE II no Switch usando PS2Recomp, mantendo o caminho gráfico GS padrão funcional. A substituição experimental do GS e o viewport expandido/16:9 ficam preservados atrás de opções de compilação desligadas por padrão.

## Aceitação acordada

1. **Primeiro marco visual:** chegar ao mapa da primeira campanha e inspecionar o personagem inicial e o terreno revelado em torno dele. O resto do mapa preto pode ser fog of war; não é critério de falha sem evidência de defeito.
2. **Primeiro marco de controle:** provar navegação, seleção, ordens, câmera, HUD, minimapa, texto, pausa e retorno ao jogo.
3. **Primeira versão funcional:** concluir o tutorial, salvar/carregar uma partida em andamento e retornar ao menu.
4. Validação inicial no Ryujinx; validação em console físico permanece pendente.
5. Dispositivos obrigatórios: Joy-Con acoplados e Pro Controller. Joy-Con único ou par destacado ficam fora da primeira versão.
6. Debug e diagnósticos permanecem até a versão Debug passar pelos marcos. Depois disso, cada wrapper de build deve exigir seleção explícita de Debug, Release ou RelWithDebInfo e a versão de produção repete os marcos.

## Configuração de compilação

As opções CMake `AOE2_EXPERIMENTAL_EXPANDED_VIEWPORT` e `AOE2_EXPERIMENTAL_NATIVE_GRAPHICS` ficam OFF por padrão; `tools/docker_build.py` as expõe como `--experimental-expanded-viewport` e `--experimental-native-graphics` somente no alvo Switch. A primeira controla realocação/reserva exclusiva de mapa, expansão e layout 16:9 com sua transformação de entrada. A segunda controla captura/composição nativa que substitui a apresentação GS. Nem `__SWITCH__`, Debug, GLES, ambiente nem `map_view.ini` podem ativar uma opção desligada.

O wrapper sempre grava explicitamente ON/OFF no CMake, para uma configuração experimental anterior no cache não vazar para a build padrão. A configuração padrão deve omitir dos alvos do jogo as unidades de compilação exclusivas dessas experiências e usar o frame GS completo do PS2Recomp no único apresentador GLES Switch existente. O caminho Switch atual, que força `NativeFrameProvider` parcial, só pode ser instalado quando `AOE2_EXPERIMENTAL_NATIVE_GRAPHICS` estiver ligado. O suporte de GLES do runtime continua necessário para apresentar o frame GS e não é removido.

Manter 64 MiB de RAM e proteções de heap/HLE/stacks. Condicionar somente a reserva dedicada de viewport e seus hooks à opção da viewport; testar limites de heap, ELF, aliases e arena em ambas as configurações. Quando a substituição gráfica for ligada sem a expansão, limitar `map_view.ini` e `AOE2_MAP_VIEW` a `legacy` para evitar consumir a arena. Com flags desligadas, configurações experimentais externas são ignoradas com aviso; sem inicialização parcial ou tela preta silenciosa.

| Viewport | Gráficos nativos | Uso                                       | Estado esperado                                                     |
| -------- | ---------------- | ----------------------------------------- | ------------------------------------------------------------------- |
| OFF      | OFF              | Configuração padrão para Switch funcional | GS/frame legado completo; sem hooks experimentais de mapa/presenter |
| ON       | OFF              | Pesquisa isolada de viewport              | Reserva e captura de mapa; apresentação de referência GS            |
| OFF      | ON               | Pesquisa isolada de substituição gráfica  | Presenter nativo atrás da flag; não prometer cobertura completa     |
| ON       | ON               | Experimento combinado                     | Viewport e composição nativa; não é configuração de produção        |

## Controles

- Manter equivalência de face com o pad PS2: Switch B→Cross, A→Circle, Y→Square, X→Triangle; L/R→L1/R1; ZL/ZR→L2/R2; Plus/Minus→Start/Select; cliques de analógico→L3/R3.
- Validar os sticks esquerdo/direito, inversão vertical, centro, extremos e zona morta. Validar press, hold, release, conexão e desconexão sem botões presos para Joy-Con acoplados e Pro Controller.
- Pointer absoluto deve seguir a imagem apresentada: touch no espaço real do painel e mouse absoluto do HID mapeiam pelo retângulo de apresentação compartilhado com o cursor guest. Conferir margens, bordas, botões, arrasto e alinhamento com hit testing. O estado sem entrada não move o cursor.
- Troca de resolução/modo atualiza uma única transformação render/input. O ponteiro não usa dimensões fixas 1280×720 quando o frame ou janela mudar.

## Acompanhamento visual e automação

1. Iniciar cada cenário com captura da tela e identificação do estado atual. Enviar uma ação apenas quando o estado esperado para ela foi observado.
2. Capturar tela e estado antes e depois de cada ação. No avanço de narrativa, enviar um clique absoluto por vez e só continuar se a imagem mudar; limitar a doze cliques. Ao aguardar o mapa, observar e capturar a cada dez segundos sem enviar botões. `black`, falha de captura, ausência de transição ou timeout interrompe a sequência.
3. Substituir séries cegas de botões e sleeps longos por checkpoints curtos. Durante carga demorada, inspecionar a cada dez segundos; continuar somente com progresso visível/verificável. Não sobrepor driver e entrada manual.
4. Inspecionar visualmente o primeiro mapa e o entorno do personagem. Usar TTY/logs para apoiar diagnóstico, nunca como prova única de tela, mapa ou controle funcionais.
5. Registrar a sequência temporal: build/hash, flags, entrada emitida e liberação, estado observado, screenshot, log e motivo de espera/falha. Preservar capturas de falha para corrigir detector ou runtime antes de repetir.

## Sequência de implementação

1. **Baseline e plano persistido:** guardar uma sessão/log de referência, registrar hash e testar detector de telas antes de alterar o caminho principal. Manter este arquivo como plano ativo e atualizar os gates à medida que forem concluídos.
2. **Flags e caminho GS:** introduzir as opções, podar os alvos do jogo, desligar o `NativeFrameProvider` por padrão e fazer o Switch apresentar o frame GS completo. Atualizar o fork diretamente; registrar pin e geração com proveniência verdadeira quando aplicável.
3. **Builds independentes:** gerar NRO Debug com flags OFF e verificar as quatro combinações via `python3 tools/docker_build.py --platform switch [--experimental-expanded-viewport] [--experimental-native-graphics]`. Confirmar no grafo/compilation commands que fontes experimentais não entram em OFF/OFF e que overrides de ambiente/INI não as reativam.
4. **Controles:** corrigir e testar Joy-Con acoplado e Pro Controller. Corrigir o alinhamento touch/mouse/cursor usando o retângulo realmente apresentado e estados de entrada sem drift.
5. **Ryujinx visual:** acompanhar boot, cartão/perfil, menu, campanha/narrativa e mapa com screenshots. Atingir o primeiro marco, então testar ações de gameplay, tutorial, pausa, retorno e save/load.
6. **Build configurável:** após a aceitação Debug, remover Debug forçado dos wrappers, docs e skill local de build. Exigir uma configuração explícita em cada wrapper/preset, manter logs verbosos e separar diretórios por plataforma/configuração/flags. Revalidar o NRO de produção sem otimizações ou macros que alterem silenciosamente os contratos guest.

## Validação e registro

- Em cada gate, rodar testes direcionados de flags, pad, ponteiro, mapa/memória e presenter, depois suítes C++/Python pertinentes. Builds de bring-up seguem Debug e alta verbosidade conforme a política vigente até o Gate 5.
- Usar o workflow Docker Switch para NRO. O primeiro gate funcional exige screenshots reais inspecionados no Ryujinx com o personagem e terreno local visíveis; o gate completo exige tutorial e save/load.
- Após habilitar tipos de build, repetir o fluxo na configuração escolhida para produção. Registrar separadamente sucesso de build, boot, mapa inicial, interação, conclusão do tutorial e save/load.
- Não chamar indicador de submissão, probe de inicialização, tela de loading ou teste sintético de aceitação de gameplay.
- Após os gates, atualizar este plano, [índice](../README.md), [arquitetura](../architecture.md), [arquitetura Switch](../switch-target-architecture.md) e SOTs gráficos. Marcar explicitamente os modos experimentais, estado de cada combinação e pendência de teste físico.
