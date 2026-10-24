# Shim nativo do módulo de arquivos (primeira fatia da troca do runtime abaixo do motor)

Sem endereços, nomes de funções nem trechos do binário (regras de proveniência). Papéis e contagens apenas;
os detalhes por função ficam em artefatos ignorados (`analysis/generated/`, `game/`).

## Intenção

Objetivo final: compilar e executar o jogo no Nintendo Switch. O caminho de hoje é
`Switch → runtime PS2Recomp → KCET → motor`. O dono quer `Switch → shim nativo → motor`: o PS2Recomp
passa a ser **só o gerador** do código do motor, e o que fica abaixo da fronteira é trocado, módulo por
módulo, por implementações nativas escritas a partir do decomp do KCET (`~/repos/aoe`, autorizado pelo dono).
Este documento cobre o **primeiro módulo: arquivos** (camada de arquivos do KCET). Os demais módulos repetem
o mesmo processo e terão seus próprios documentos.

Estende `docs/plans/native-adapter-boundary.md` (contrato do adaptador, mapa por módulo) com a decisão de
arquitetura abaixo e a primeira entrega concreta.

## Decisões do dono (2026-10-23)

1. O código gerado do motor **permanece**, gerado automaticamente, sem edição manual e sem stubs. Isso vale
   para tudo da região do motor em diante (engine, IA, combate, construção, menus).
2. A região abaixo do motor (KCET e camada de compatibilidade, incluindo os wrappers de CD/SIF que o módulo
   de arquivos chama) é **substituída**, módulo por módulo, por código nativo baseado no decomp.
3. Do runtime upstream **fica**: contexto de CPU R5900, memória guest, tabela de funções e escalonador
   (`EeScheduler`); possíveis exceções adicionais ficam em aberto e serão justificadas caso a caso.
4. Começar pelo módulo de arquivos e ir "um a um".
5. Verificação: **teste diferencial onde der** (original recompilado × nativo, mesmas entradas, mesmos
   retornos e mesma memória escrita). Onde o original depende de hardware (leitura de setores), a referência é
   o contrato extraído do decomp.

## O que o módulo faz (do decomp, nível de papel)

Cerca de 50 funções, duas metades:
- **Tabelas de arquivos**: busca binária e linear por id ou por nome em registros de 8 bytes, e uma tabela de
  despacho por "tipo" (4 tipos; tipo inválido cai em um relatório de erro de asserção).
- **Wrappers de CD**: leitura de setores, troca/estado de disco e consultas, todos via libcdvd/SIF.

O decomp é correspondência de bytes, mas usa nomes genéricos e tipos `void *`; os formatos dos registros e o
significado dos tipos vêm do assembly correspondente, que é a especificação final. Não há header do módulo no
decomp.

## Cobertura do decomp (reverificada em 2026-10-24)

A leitura de 2026-10-23 estava desatualizada: o fonte do servidor de CD foi preenchido depois (de ~620 para
~5.600 linhas, nenhum corpo vazio). Estado atual dos fontes do decomp usados aqui:
- **kernel**: C estruturado, completo.
- **gpu**: parcial (despachante de comandos GS e alguns manipuladores).
- **servidor de CD**: ~150 funções com lógica, mas em **forma literal do assembly** (registradores emulados
  como variáveis `long`, rótulos e `goto`), não C estruturado. Os acessores de que o módulo de arquivos
  depende são triviais (leitura de globais, extração de campos de 24 bits); as maiores são o servidor RPC.
- **módulo de arquivos**: C estruturado, ~50 funções.
- Consequência: o servidor de CD serve como especificação de comportamento, mas deve ser conferido contra o
  assembly e contra o original recompilado (teste diferencial) antes de ser tratado como contrato; a forma
  literal pode esconder diferenças de largura/sinal.

## Objetivos

- Atender cada ponto de entrada do módulo na fronteira (substituição por papel, mecanismo já existente no
  registro de hooks) com um **modelo de host puro**, sem `R5900Context`, mais um handler fino que traduz
  registradores e memória guest.
- Leituras de disco vão para a imagem do disco ou arquivos soltos do host; nenhum SIF/IOP/libcdvd é executado
  por esse caminho.
- Lógica puramente de memória (as buscas em tabelas) é portada fielmente e coberta por teste diferencial.
- Onde o original bloquearia, o adaptador dorme a latência modelada; nunca devolve em laço apertado (lição do
  travamento de boot causado por uma substituição que não imitava o bloqueio).
- Entregável portável (x86-64 hoje, ARM64/Switch depois): sem dependência de SO no modelo.

## Não-objetivos

- Trocar o escalonador, a CPU, a memória ou a tabela de funções do upstream.
- Os outros módulos do KCET e o GS/GPU (documentos próprios).
- Editar `third_party/PS2Recomp`, o código gerado ou o decomp.
- Resolver o travamento atual do jogo (pump de mensagens, `ra=0`): independe desta entrega.

## Anti-objetivos (falha mesmo que "funcione")

- Retorno inventado ou stub para fazer o jogo andar.
- Nome, endereço ou trecho do binário em arquivo versionado.
- Dois caminhos ativos para a mesma função (original e nativo) sem decisão explícita por papel.
- Declarar sucesso só porque o jogo não travou: a prova é o teste diferencial e o contrato do decomp.

## Restrições

- Proveniência e licença conforme `CLAUDE.md` e `docs/sot/prior-art-and-reuse.md`; PS2Recomp é GPL-3.0.
- Endereços resolvidos localmente pelo resolvedor ancorado no ELF (`kcet_hooks.ini`, ignorado); um papel
  novo exige âncora única e verificada, senão falha.
- Build apenas pelos caminhos documentados (memória limitada, locale C); Docker para plataformas.
- TDD: teste vermelho antes do código; um commit por fatia.

## Estratégia de verificação (diferencial)

- Harness reutilizável: roda a função **original recompilada** sob o runtime com estado guest sintético
  (tabelas montadas à mão), grava entradas → (retorno, bytes escritos), e roda o nativo com o mesmo estado.
  Igualdade exigida. É a mesma infraestrutura que servirá aos próximos módulos.
- Classes: (a) puras de memória → diferencial completo; (b) dependem de hardware → teste contra o contrato do
  decomp com leitor de disco em memória; (c) chamadas de erro/asserção → verificar a mesma ramificação.
- Validação final: `validate_desktop.py` mostra o jogo passando pelo caminho de arquivos sem tocar SIF/IOP,
  com a medida registrada em `docs/sot/kcet-stall-log.md` (sem endereços).

## Deixado ao construtor

- Estrutura de arquivos e nomes dos novos módulos de host.
- Formato exato do harness e do gravador de casos.
- Como mapear tipos e estruturas do módulo para tipos do host.
- Ordem das funções dentro do módulo (sugestão: puras primeiro, depois CD).

## Decisões minhas (a revisar)

- **Escopo de hooks:** substituir todo ponto de entrada do módulo chamado pelo motor ou por outro ponto de
  entrada do módulo. Um traço de execução (função original chamada = falha) prova que nenhum chamador
  alcança o original; sem isso a função não é dada como substituída. Folhas e chamadas por vtable entram pelo
  traço, não pelo inventário estático.
- **Operação sem dados ou fora do esperado:** devolve exatamente o código de erro que o decomp mostra para o
  caso (ou o mesmo caminho de asserção), nunca um valor novo. Se o decomp não define o caso, falha explícita
  com log e o caso vira item de revisão, não um padrão inventado.

## Decidido pelo dono (2026-10-23)

- Origem dos dados de disco no Switch: **arquivos já extraídos no SD card, incluindo os assets já
  extraídos**; não há leitor de imagem ISO no alvo. O modelo de leitura resolve o pedido (id/posição de disco)
  para um caminho de arquivo extraído. Consequência a resolver na construção: como mapear leituras por
  posição de setor para arquivos e deslocamentos (índice gerado do disco em uma etapa de importação,
  ignorado pelo git).

## Em aberto (técnico)

- Quais pontos de entrada do módulo o motor chama de fato (o inventário de fronteira dá um piso; arestas por
  folhas e por vtable não entram).
- Se algum chamador interno do módulo, abaixo da fronteira, ainda chega ao original substituído.
