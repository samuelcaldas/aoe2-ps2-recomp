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

- Fronteira KCET: **58 pontos de entrada**, chamados por **40 funções** do motor. É superfície pequena.
- O decomp (`~/repos/aoe`) já tem 100% de correspondência de bytes nos módulos KCET: mouse, teclado,
  driver USB de teclado, pad, servidor CD/DVD, arquivos, memory card, kernel, hd e libscf, mais a rede.
  Isso dá o comportamento exato (máquinas de estado, layouts de buffer, códigos de retorno).
- O mecanismo de substituição já existe e funciona: registro `kcet_hooks.ini` + `replaceFunction`,
  resolvedor ancorado no ELF, modelos de host testados (mouse, teclado, sockets i.LINK).
- Evidência de que a emulação do IOP é o lado frágil: o travamento de boot foi causado por uma
  substituição que **não imitava o bloqueio** de uma RPC (o receptor matou de fome a thread do jogo).
  Adaptadores que respondem na fronteira de alto nível evitam essa classe inteira de erro.

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
5. **Proveniência**: adaptadores são código novo em termos de papéis, endereços resolvidos localmente.
   O código portado do decomp serve de especificação; não entra nome/endereço/trecho em arquivo versionado.

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

1. **Inventário da fronteira completa**: KCET (58), mais bibliotecas Sony e syscalls EE usadas pelo motor.
   Classificar cada ponto: já substituído, adaptador novo, ou fica (kernel). Saída: tabela de papéis.
2. **Contrato e harness de diferença**: formato de gravação/reprodução (entrada → saída) sobre o
   original recompilado; primeiro módulo piloto pequeno (pad ou file).
3. **Módulos por prioridade de boot**: file/cdvd → kernel/áudio de boot → pad/mc → resto.
4. **Parar de carregar IRX/SIF** quando o último consumidor do boot for coberto; medir o boot.
5. **Validação**: `validate_desktop.py` com 240 s, frames reais e mudando, mais testes de diferença.
6. **Longo prazo (opcional)**: trechos do motor com decomp completo podem virar código nativo e sair do
   PS2Recomp. Não é meta desta fase.

## Decisões em aberto

1. **Kernel EE** (threads/semáforos/interrupções): manter o `EeScheduler` do runtime (o motor recompilado
   ainda usa syscalls) ou substituir por threads nativas. Recomendação: manter por ora.
2. **Fronteira Sony abaixo do KCET** (libgraph/libdma/libkernl): medir antes de decidir; o motor pode
   chamá-las diretamente, e elas dependem de hardware (GS/DMA).
3. **Contrato com o agy**: formato em que a decompilação da camada chega aqui (documento de
   comportamento por papel, e não só código), para eu não depender de ler fontes brutas.
4. **Proveniência**: confirmar que adaptadores descritos por papel, sem nomes/endereços, satisfazem a
   exceção autorizada pelo dono (CLAUDE.md), antes de portar qualquer lógica.

## O que muda no trabalho já feito

- **Mantém**: registro de hooks, resolvedor, modelos de host (mouse, teclado, sockets), patch de despacho
  de overlay, pipeline de overlays, validadores.
- **Deixa de ser meta**: fidelidade de IOP/SIF/IRX, e remendar o runtime upstream para essa camada.
- **Corrigir**: o "ra=0 do pump de mensagens" segue sendo o bloqueio imediato e independe desta mudança.
