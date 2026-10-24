# Camada KCET nativa completa (substitui o runtime abaixo do motor)

Estende `2026-10-23-file-module-native-shim-design.md`, que passa a ser o detalhamento do módulo de arquivos
dentro deste escopo maior. Em conflito de ordem ou escopo, vale este documento.

Estado: **rascunho para aprovação do dono.** Decisões do dono desta rodada estão marcadas como tais; o resto
são minhas escolhas, a revisar.

## Intenção

Alvo final: Nintendo Switch. Hoje o caminho é `Switch → runtime PS2Recomp → KCET → motor`. O dono quer
`Switch → camada nativa → motor`, com o PS2Recomp só como gerador. Em 2026-10-24 o dono ampliou o escopo do
primeiro módulo de arquivos para **toda a camada KCET**: kernel, pad e vibração, mouse, teclado, arquivos e
servidor de CD, cartão de memória, `kt_hd`, GPU, rede e i.LINK. Cada peça é reimplementada para esta codebase
a partir do decomp do dono (`~/repos/aoe`), cuja cópia direta é autorizada (`CLAUDE.md`).

## Decisões do dono

1. **Motor e overlays continuam gerados pelo PS2Recomp**, sem edição manual e sem stubs. Só o que fica abaixo
   deles é trocado (2026-10-24). Os overlays (lobby, menus, ajuda, histórico etc.) são lógica de jogo em forma
   literal do assembly, ~150 mil linhas no decomp; reescrevê-los está fora do escopo.
2. **Endereços sobrepostos dos overlays:** o PS2 carrega overlays diferentes nos mesmos endereços, trocando-os
   da memória. Em PCs modernos com muita RAM isso não faz sentido; a tradução dos overlays deve levar isso em
   conta (ver "Em aberto").
3. **Ordem de trabalho aprovada** (2026-10-24), com um interruptor por módulo entre nativo e original:
   1. kernel (semáforos, threads, vsync)
   2. pad e vibração
   3. mouse, teclado, teclado USB
   4. arquivos e servidor de CD, juntos (leitura de disco)
   5. cartão de memória
   6. `kt_hd`
   7. `kt_gpu`
   8. rede e i.LINK, quando houver decomp dos sockets
4. Do runtime upstream ficam: contexto R5900, memória guest, tabela de funções, `EeScheduler`; exceções caso a
   caso (decisões de 2026-10-23, mantidas).
5. Dados de disco vêm de arquivos já extraídos no SD, incluindo assets (2026-10-23, mantida).
6. **Teste diferencial fica para depois** da implementação das camadas (2026-10-24), com a ressalva abaixo.

## Estado dos módulos

- **kernel:** nativo, atrás do interruptor `kernel` (38 de 39 funções; uma segue gerada). Aguardando verificação
  diferencial e uma medição mais longa; resultado de 60 s em `docs/sot/kcet-stall-log.md` (sem mudança de veredito).
  Plano: `docs/superpowers/plans/2026-10-24-native-kernel-module.md`.
- Demais módulos: sem plano ainda, na ordem acima.

## Cobertura do decomp (medida em 2026-10-24)

- Estruturado: kernel, arquivos, `kt_hd`, mouse, teclado, teclado USB, `libscf`, `sles_boot`, pad (~1.700 linhas).
- Parcial: GPU (~145 linhas).
- Forma literal do assembly (registradores `long`, `goto`): servidor de CD (~9.600 linhas), cartão de memória
  (~11.200 linhas). Servem de especificação; não compilam como C limpo para o host sem trabalho.
- Rede: o decomp tem a camada DirectPlay do motor (C++ estruturado, ~1.100 linhas), mas **não** os wrappers de
  socket (`sceILsock*`) nem o lado IOP dos módulos i.LINK. Esses precisam ser decompilados antes (plano de rede
  anterior, etapa N0).
- Vibração: não verificado onde fica; hipótese de que está no módulo de pad.

## Objetivos

- Cada módulo da lista acima atendido por código nativo (decomp copiado com origem anotada, mais uma camada
  de adaptação), sem SIF/IOP/libcdvd executados por ele.
- Interruptor por módulo, ligado por configuração, para isolar regressões.
- Um commit por módulo; módulo só é dado como substituído quando um traço de execução mostra que o original
  não é mais chamado (decisão de 2026-10-23, mantida).
- Entregável portável (x86-64 hoje, ARM64/Switch depois).

## Não-objetivos

- Reescrever motor ou overlays; trocar escalonador, CPU, memória ou tabela de funções do upstream.
- Editar `third_party/PS2Recomp`, código gerado ou o decomp.
- Resolver agora o travamento de boot (`ra=0`, semáforo do servidor de CD); ele é investigado em paralelo e as
  substituições não são dadas como correção dele sem evidência.

## Anti-objetivos

- Retorno inventado ou stub para fazer o jogo andar.
- Binário, disassembly, endereço ou nome do binário em arquivo versionado fora da exceção do decomp.
- Dois caminhos ativos para a mesma função sem interruptor explícito.
- Declarar sucesso porque o jogo não travou.

## Risco assumido por adiar os testes (minha ressalva)

O travamento atual tem causa desconhecida. Trocar vários módulos sem verificação por módulo dificulta isolar
qual troca ajuda ou piora. Mitigação: interruptor por módulo, commit por módulo, traço de execução de
cobertura, e teste unitário nas partes puras. O harness diferencial entra depois, sem mudar a estrutura.

## Restrições

- Proveniência: `CLAUDE.md` e `docs/sot/prior-art-and-reuse.md`. Nunca versionar binários, ISOs, exportações
  do Ghidra, mapas de função, disassembly, C++ gerado.
- Endereços de entrada resolvidos localmente por âncora no ELF (arquivo ignorado); falha se ambíguo.
- Build só pelos caminhos documentados; devkitPro só em Docker.

## Deixado ao construtor

- Estrutura de diretórios e nomes dos módulos de host; formato do script de cópia do decomp.
- Como adaptar os módulos em forma literal (cartão de memória, servidor de CD): reescrita fiel ao comportamento
  ou compilação da forma literal com a ponte.
- Formato do interruptor por módulo.

## Em aberto

- **Sobreposição de overlays:** como o ELF carrega os segmentos executáveis sem conteúdo em endereços que se
  repetem, e como o gerador e a tabela de funções os distinguem. Opções a avaliar: manter todos residentes em
  regiões de memória guest distintas (exige tratar chamadas e ponteiros absolutos), ou manter o esquema de
  troca do original. Exige análise antes de decidir; não decidido.
- Local do lado IOP dos módulos i.LINK no decomp.
- Onde a vibração é implementada.
- Mapeamento de posição de setor para arquivo extraído.
- Serviço de motor ainda sem dono e entradas de fronteira desconhecidas (12 KCET, 5 Sony).
