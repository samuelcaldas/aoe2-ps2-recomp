# Plano ajustado: adaptadores nativos na fronteira da camada de compatibilidade

Substitui, para a camada de compatibilidade do PS2, a estratégia anterior (recompilar e emular
IOP/SIF/IRX e remendar os sintomas). Não altera `vectorized-sleeping-walrus.md`. Sem endereços nem
nomes de funções do binário (regras de proveniência): papéis e contagens apenas.

## Princípio

O jogo é um motor Windows (Genie, compilado com MWCC) embrulhado por uma camada PS2 (KCET + bibliotecas
Sony + serviços do IOP). A camada de compatibilidade **não deve ser traduzida nem executada**. O motor
continua recompilado (PS2Recomp), mas toda chamada que ele faz para a camada é atendida por um
**adaptador nativo** que devolve o que o original devolveria. O que está abaixo da fronteira (impl. KCET,
bibliotecas Sony, SIF, módulos IRX, emulação de IOP) deixa de rodar.

```
 motor (recompilado)  ──►  FRONTEIRA (papéis kcet.*)  ──►  adaptador nativo  ──►  host (input, arquivos, rede, áudio, vídeo)
                                  ╳  nada abaixo executa: KCET · libs Sony · SIF · IOP/IRX
```

## Por que isso é viável (medido)

- Fronteira que exige adaptador: ~85 pontos de entrada (Sony base + KCET). É superfície pequena (ver medida abaixo).
- O decomp (`~/repos/aoe`) já tem 100% de correspondência de bytes nos módulos KCET: mouse, teclado,
  driver USB de teclado, pad, servidor CD/DVD, arquivos, memory card, kernel, hd e libscf, mais a rede.
  Isso dá o comportamento exato (máquinas de estado, layouts de buffer, códigos de retorno).
- O mecanismo de substituição já existe e funciona: registro `kcet_hooks.ini` + `replaceFunction`,
  resolvedor ancorado no ELF, modelos de host testados (mouse, teclado, sockets i.LINK).
- Evidência de que a emulação do IOP é o lado frágil: o travamento de boot foi causado por uma
  substituição que **não imitava o bloqueio** de uma RPC (o receptor matou de fome a thread do jogo).
  Adaptadores que respondem na fronteira de alto nível evitam essa classe inteira de erro.

## Fronteira medida (2026-10-23, `tools/boundary_inventory.py`)

Método: o motor (MWCC) salva `ra` com `sq`; KCET, bibliotecas Sony e auxiliares EE-GCC usam `sd`. Uma aresta
é uma chamada direta ou salto de cauda de uma função do motor para uma função EE-GCC ou de syscall.
O relatório com endereços fica em `analysis/generated/boundary_inventory.json` (ignorado).

| Região | Pontos de entrada | Funções do motor que chamam | Precisa de adaptador? |
|---|---:|---:|---|
| Syscalls EE (kernel) | 15 | 79 | Não por ora: `EeScheduler` fica (decisão 1) |
| Bibliotecas Sony base (SIF, DMA, pad, cd, mc, gráficos) | 37 | 137 | **Sim**: toca hardware e IOP |
| KCET (mouse, teclado, pad, cdvd, file, mc, kernel, hd) | 48 | 34 | **Sim**: é o alvo principal |
| Auxiliares de C e libgcc (formatação, mem/str, qsort, float→int) | 30 | ~420 | Não: cálculo puro; podem seguir recompilados |
| Fora da faixa (um ponto isolado) | 1 | 1 | A investigar |

Leitura: cerca de **85 pontos de entrada** exigem adaptador (Sony base + KCET), chamados por poucas
dezenas a ~170 funções do motor. O ponto mais quente (376 chamadores) é um auxiliar de float para inteiro
do libgcc: puro, sem hardware, não é fronteira de compatibilidade.

Limites da medida: a classificação é heurística. Das ~8.100 funções, 2.040 são folhas sem frame e ficam
sem classe, então arestas do motor para uma folha de compatibilidade **não são contadas**; os números são
um piso. Chamadas indiretas (vtables, ponteiros de função) também não entram. Validar com o log de
execução (RPCs SIF vistos, syscalls) antes de dar o inventário por completo.

### Papéis por evidência (2026-10-23, `tools/boundary_roles.py`)

Para cada um dos 116 pontos de entrada, segue as chamadas diretas que não entram no motor e coleta ids de
serviço SIF (mapeados a papéis), bases de registradores de hardware e syscalls. Sem evidência, o papel
é `unknown`; nada é adivinhado. Os ids pequenos de comando do próprio SIF são tratados como transporte.

| Papel | Pontos de entrada | Onde estão | Disposição |
|---|---:|---|---|
| kernel (alcança syscalls) | 54 | 25 no KCET (envoltórios de threads/semáforos/seção crítica), 19 Sony base, 8 C | Mapear ao `EeScheduler` (decisão 1); sem RPC |
| unknown (sem evidência) | 37 | 20 auxiliares C/libgcc, 12 KCET, 5 Sony base | C/libgcc: puros, seguem recompilados. KCET/Sony: lógica de memória (filas, estado); revisar um a um |
| sif_transport | 10 | Sony base e KCET | Some junto com o SIF: nada abaixo da fronteira executa |
| hardware (DMA/GIF/GS/VIF) | 9 | 7 Sony base, 2 KCET | Ligam ao rasterizador/GS (fase própria, ver ADR 0001) |
| cdvd | 2 | Sony base e KCET | Leitura nativa da ISO/arquivos soltos |
| audio_sd (serviço de volume do SD) | 1 | KCET, chamado 3 vezes pelo motor | Adaptador de áudio do host |
| pad, memory_card, usb_mouse, usb_keyboard | 1 cada | KCET | Adaptadores de entrada e de `mc0/` |

Serviços SIF ligados pelo binário (busca de binds, `service_bind_sites`): 28 ids. Por papel: cdvd (5), socket
i.LINK (12), pad (2), memory card (1), mouse (1), teclado (1), loadfile (1), áudio SD (2 ids pequenos),
configuração de rede (1 id pequeno, texto `ktNetCnf`), o comando de transporte (1), e **um id pequeno ligado
direto pelo motor sem texto identificador** (a identificar pelo decomp). Os ids pequenos são nomeados pelas
strings vizinhas ao bind, não por tabela. O log de execução vê 5 serviços e todos aparecem na lista estática;
o resto ainda não foi exercido porque o boot não chegou lá.

Conclusão operacional: dos 116 pontos, os que **precisam de lógica nova** são poucos (cdvd/file, pad, cartão,
mouse/teclado, áudio, hardware gráfico). O grosso é kernel (já coberto pelo escalonador) e auxiliares puros.

## Contrato do adaptador

1. **Escopo**: cada papel `kcet.<módulo>.<operação>` ou chamada Sony usada pelo motor tem um adaptador.
2. **Comportamento**: derivado do decomp (entradas, saídas, efeitos em memória do guest, retornos).
   Escrito como modelo de host puro (como `UsbMouseRpcModel`/`IlinkSocketModel`), sem depender de
   `R5900Context`; um fino handler traduz registradores/memória do guest para o modelo.
3. **Bloqueio**: onde o original bloquearia, o adaptador dorme por latência modelada (padrão já validado),
   nunca devolve imediatamente em laço.
4. **Verificação por diferença** (o ganho principal): o código original recompilado ainda existe no
   binário. Em testes, rodar o original sob o runtime, gravar pares (entrada → saída/efeitos), e exigir
   que o adaptador reproduza o mesmo. Sem captura de hardware, essa é a referência mais forte.
5. **Proveniência (atualizado 2026-10-24)**: o dono autorizou copiar o código do decomp para este repositório
   (ver `CLAUDE.md`). Adaptadores podem ser o C do decomp compilado para o host, com indicação de origem
   (repositório e commit) e uma ponte de adaptação separada. Continuam fora do git: binários, ISOs, exportações
   do Ghidra, mapas de funções, disassembly e C++ gerado. Endereços resolvidos a partir do ELF ficam em ignorados.

## Mapa por módulo (disposição)

| Módulo | Substituição nativa |
|---|---|
| mouse, teclado, pad | Já parcial. Passam a ser adaptadores de entrada do host; pad via gamepad do host. |
| cdvd server, file | Leitura direta da ISO/arquivos soltos; sem RPC. O carregador de overlay passa a ser conhecido pelo adaptador. |
| memory card | Diretório do host (`mc0/`) com a semântica de retornos do decomp. |
| kernel (threads, semáforos) | Ver decisão 1. |
| rede (i.LINK) | Já feito na fronteira `sceILsock*`; sem IRX. |
| áudio (libsd/SD) | Adaptador de áudio do host na API do jogo; os IRX de som deixam de carregar. |
| gpu | Fora do KCET real (ver ADR 0001): costura é o rasterizador do GS. Fase própria. |
| serviços do boot (SIF/IOP) | Deixam de existir: nada abaixo da fronteira executa. |

## Overlays

- Tratados como motor (código MWCC), recompilados como já feito (patch de despacho + pipeline).
- O adaptador de arquivos conhece **qual overlay o jogo pediu e quando carregou**, o que substitui a
  chave por id lido da memória por um sinal explícito e elimina adivinhação.
- Alvo único segue sendo EN; overlays de outros idiomas são recusados por verificação explícita.

## Etapas

1. **Inventário da fronteira completa**: medida estática e papéis feitos (tabelas acima). Falta identificar o serviço
   de id pequeno ligado direto pelo motor e revisar um a um os `unknown` do KCET/Sony base.
2. **Contrato, harness de diferença e cópia do decomp**: script que copia os fontes do decomp para o repositório
   com a origem registrada, mais a ponte de memória/tipos do host (design em
   `docs/superpowers/specs/2026-10-23-file-module-native-shim-design.md`).
   **Contrato e harness de diferença**: formato de gravação/reprodução (entrada → saída) sobre o
   original recompilado; primeiro módulo piloto pequeno (pad ou file).
3. **Módulos por prioridade de boot**: file/cdvd → kernel/áudio de boot → pad/mc → resto.
4. **Parar de carregar IRX/SIF** quando o último consumidor do boot for coberto; medir o boot.
5. **Validação**: `validate_desktop.py` com 240 s, frames reais e mudando, mais testes de diferença.
6. **Longo prazo (opcional)**: trechos do motor com decomp completo podem virar código nativo e sair do
   PS2Recomp. Não é meta desta fase.

## Decisões em aberto

1. **Kernel EE** (threads/semáforos/interrupções): **decidido (2026-10-23): manter o `EeScheduler`
   provisoriamente.** O motor recompilado ainda usa as syscalls; reavaliar quando o resto estiver nativo.
2. **Fronteira Sony abaixo do KCET** (libgraph/libdma/libkernl): medir antes de decidir; o motor pode
   chamá-las diretamente, e elas dependem de hardware (GS/DMA).
3. **Contrato com o agy**: **decidido (2026-10-23): só o código decompilado.** Este projeto lê os fontes de
   `~/repos/aoe` e extrai o comportamento; o agy não precisa produzir documentos extras. Consequência: o
   comportamento extraído por papel deve ser registrado aqui (sem endereços/nomes) para ser revisável.
4. **Proveniência**: confirmar que adaptadores descritos por papel, sem nomes/endereços, satisfazem a
   exceção autorizada pelo dono (CLAUDE.md), antes de portar qualquer lógica.

## O que muda no trabalho já feito

- **Mantém**: registro de hooks, resolvedor, modelos de host (mouse, teclado, sockets), patch de despacho
  de overlay, pipeline de overlays, validadores.
- **Deixa de ser meta**: fidelidade de IOP/SIF/IRX, e remendar o runtime upstream para essa camada.
- **Corrigir**: o "ra=0 do pump de mensagens" segue sendo o bloqueio imediato e independe desta mudança.
