# Plano: contrato de CheckDiskMedia e integridade de leitura CDVD

## Context

O trabalho anterior de SCmd/SMENU já foi integrado em `main`. O build diagnóstico compila e sinaliza o semáforo SCmd, mas a execução de 30 s continua em `NOW LOADING...`, repetindo `CheckDiskMedia failed`, sem solicitar o SMENU. O objetivo agora é corrigir a primeira divergência demonstrável da verificação de mídia e eliminar leituras falsamente bem-sucedidas, depois medir o avanço real do boot.

> **Execução:** usar `superpowers:executing-plans`, com TDD por tarefa e uma revisão independente ao final. Este documento é planejamento; nenhuma correção abaixo foi aplicada.

**Spec:** solicitação do usuário nesta conversa; evidência anterior em `docs/scmd-smenu-validation.md`.
**Arquitetura:** preservar a lógica original de verificação de mídia onde a substituição nativa não implementa seu contrato. O caminho SIF/CDVD existente continua sendo responsável pelo acesso real ao disco. Tornar o modelo isolado estrito sobre bytes/erros, sem presumir que isso, sozinho, conserta o boot.
**Tecnologias:** C++20, stdlib Python, CMake/Ninja, PS2Recomp, Docker `docker-dev`, Xvfb.

## Descobertas que orientam o plano

1. **A mensagem e o chamador foram localizados.** `func_0012A650` usa a mensagem `CheckDiskMedia failed`; a referência foi conferida no ELF local. A implementação original consulta a disponibilidade do disco e chama `func_00129B10`. O resultado zero deste último é tratado como a rota de aceitação; outros resultados têm rotas distintas de repetição/rejeição. Não confundir esse resultado com o valor de disponibilidade do disco.
2. **O retorno 2 de DiskReady não é, por si, erro.** O próprio chamador original exige disponibilidade igual a 2 antes de chamar a verificação seguinte.
3. **Há uma divergência concreta na substituição nativa.** `game_hooks/src/kt_cdvd_entries.inc` classifica `func_00129B10` como `ENTRY_V(ProcessRequests, ..., m.ProcessPendingRequests())`. Porém, o original retorna uma classificação e escreve campos da estrutura recebida em `a0`. O handler nativo ignora essa estrutura e não escreve `v0`. O chamador prepara `v0=2` antes da chamada; preservá-lo é incompatível com calcular o resultado original.
4. **O modelo não recebe a fila real.** `QueueRead()` só é chamado por testes. Trocar a função original por `ProcessPendingRequests()` de um modelo sem produtor não preserva a lógica original, independentemente de corrigir sua leitura de ISO.
5. **Existe um defeito separado de integridade.** `CdvdModel::ProcessPendingRequests()` copia um buffer inicialmente zerado e conclui a operação sem verificar abertura/seek/leitura completa. A ISO esperada existe; esse defeito não demonstra, isoladamente, a causa de cada bloqueio do boot.
6. **O leitor SIF é independente.** Usa `SifCdvdServer::Disc::readToEe` e `readCdSectors()`/`readHostRange()` do runtime. Este último também merece investigação de short reads, mas não será alterado por antecipação. A verificação de limite da ISO já soma LBA/contagem em 64 bits; não registrar overflow dessa soma como defeito sem reprodução.

Referência autorizada de contrato: `../aoe/src/kcet/kt_libcdvd_server.c`, funções `func_00129B10` e `func_0012A650`, decompilação do proprietário em `eb2b12731694310da08d9b6501943169f99f4653`; confirmar novamente a revisão no início da execução. Confrontar com as funções geradas correspondentes, somente para leitura. Nomes históricos como `ProcessRequests` não substituem a análise do contrato.

## Restrições e decisões

- Não editar C++ gerado, binários, TOML para skips/stubs, ou a decompilação de referência. Nada de retornar sucesso fixo, inventar mídia ou acrescentar atalhos de semáforo.
- Não copiar disassembly, buffers de jogo, mapas ou funções geradas para arquivos rastreados. Evidência detalhada em `build/`; screenshots somente em `.playwright-mcp/`. Código copiado da decompilação autorizada exige origem/revisão.
- Preservar `.gitignore`, `third_party_provenance.lock.json` e quaisquer alterações locais alheias. Não atualizar dependências nem fazer commit/merge/push sem pedido específico.
- Não implementar uma nova fila CDVD nem unificar leitores nesta etapa. Primeiro preservar o contrato original que já existe. Não deixar correções necessárias somente em `third_party/` ignorado.
- Preservar os hooks SCmd/NCmd existentes nesta correção; validar que a função original possa executar seus waits/callbacks normalmente. Se houver outra divergência, reproduzi-la antes de ampliar o escopo.
- A ausência de `CheckDiskMedia failed` não prova boot: um processo parado antes da verificação também pode não imprimir a mensagem. Exigir evidência positiva dos marcos seguintes.

## Arquivos e responsabilidades

- `game_hooks/src/kt_cdvd_entries.inc`: limitar a tabela nativa a substituições com contrato implementado.
- `game_hooks/src/kcet_cdvd_module.cpp`: instalação e preservação das funções originais; alterar apenas se necessário para observabilidade não intrusiva.
- Novo `tests/cpp/cdvd_media_tests.cpp` e `cmake/Aoe2CdvdTests.cmake`: regressão contra as funções originais geradas, sem recompilar o jogo inteiro.
- `CMakeLists.txt`: opção `AOE2_BUILD_CDVD_RUNTIME_TESTS`, desligada por padrão; integração dos testes focados.
- `cdvd/src/cdvd_model.cpp` e `cdvd/include/aoe2/cdvd/cdvd_model.hpp`: validação de requisições e leitura exata, preservando a API pública existente.
- `tests/cpp/cdvd_model_tests.cpp`, novo `tests/cpp/cdvd_read_tests.cpp` e `tests/cpp/native_file_adapter_tests.cpp`: fixtures sintéticas, bytes e erros; separar testes para manter arquivos pequenos.
- `docs/scmd-smenu-validation.md` e `docs/sot/file-cdvd-semantics.md`: contrato observado, cobertura real e resultados, sem alegar paridade/boot não comprovados.

## Tarefa 1 — reproduzir a divergência do contrato de mídia

**Entrega:** teste que falha pelo hook atual, não por ausência de dependência, e registro local da condição original.

- [ ] Conferir branch/status, entradas do jogo e runtime; arquivar logs/capturas anteriores em caminhos ignorados antes de sobrescrever a evidência. Não rodar build/execução pesada sem validar pré-requisitos.
- [ ] Adicionar `AOE2_BUILD_CDVD_RUNTIME_TESTS` e o alvo `aoe2_cdvd_media_tests`, registrado no CTest como `cdvd_media`. Reutilizar o padrão de `cmake/Aoe2HdTests.cmake` e o runtime de testes, sem linkar a tabela completa do jogo.
- [ ] Compilar nesse alvo somente as unidades originais locais `generated/SLES-50282/FUN_00129b10_0x129b10.cpp` e `FUN_0012a650_0x12a650.cpp`, o módulo de hooks e a infraestrutura necessária. Se a opção estiver habilitada sem essas entradas, falhar no configure com diagnóstico explícito; não declarar PASS com teste omitido.
- [ ] Criar `media_check_preserves_original_return_and_output`: executar o original com memória/stack reais e resultados controlados apenas nas dependências externas. Para a rota inicial que retorna imediatamente, verificar retorno zero e campos de saída da estrutura recebida, independententes do `v0` inicial (0, 1 e 2).
- [ ] Criar `ready_disc_is_not_rejected_by_stale_v0`: executar o chamador original com disponibilidade 2 e condição de mídia original aceita. Sem override, deve aceitar sem emitir a mensagem. Com `cdvd=on`/`file_cd=on`, o comportamento deve ser idêntico; no código atual, capturar a falha causada pelo retorno/estrutura não calculados. Rodar no executor do scheduler, com timeout, se as chamadas exigirem execução guest.
- [ ] Cobrir pelo menos uma rota original de rejeição e uma de repetição com limites de teste: os testes não podem tornar toda mídia aceita. Callbacks controlados são fixtures de dependências, nunca substituições instaladas no executável do jogo.
- [ ] Registrar em `build/cdvd-media-diagnosis/` o fluxo e a primeira divergência. Se a reprodução não confirmar a inferência estática, não aplicar a correção da tarefa 2: acrescentar observação limitada de entrada/retorno/estrutura na fronteira identificada e resolver a discrepância primeiro, sem alterar respostas.

## Tarefa 2 — preservar a verificação original de mídia

**Consome:** a regressão e o contrato comprovado da tarefa 1.
**Entrega:** o módulo nativo deixa de substituir uma rotina cujo contrato não implementa.

- [ ] Remover a substituição `ProcessRequests` de `kt_cdvd_entries.inc`, registrando-a como `DEFERRED` com justificativa de contrato; manter a função original registrada pelo runtime. Não trocar `ENTRY_V` por um retorno fixo, nem simular uma fila vazia como trabalho concluído.
- [ ] Verificar instalação com `cdvd` ativo, apenas `file_cd` ativo e ambos desligados: a função de mídia original continua registrada e executa seus efeitos, enquanto os hooks válidos continuam funcionando.
- [ ] Rodar `cdvd_media` até GREEN e os testes `cdvd_model`, `native_file_adapters` e `overlay_dispatch`. Verificar retorno, estrutura guest e ausência de corrupção dos registradores preservados, não apenas ausência de mensagem.
- [ ] Fazer um build/run diagnóstico curto antes de modificar o leitor isolado. Preservar essa evidência para atribuir o avanço à correção de integração, e não misturar as duas causas.

## Tarefa 3 — impedir conclusão de leituras incompletas ou fictícias

**Entrega:** leitura exata do modelo isolado, comprovada com dados sintéticos; não anunciar esta tarefa como conserto automático da fila real.

- [ ] Criar fixture temporária com quatro setores de 2048 bytes e padrões diferentes por setor/posição. Os testes não dependem de ISO proprietário nem aceitam buffer todo zero como prova de leitura.
- [ ] Escrever e observar RED em `reads_exact_single_and_multiple_sectors`, `reads_last_sector_without_crossing_eof` e `invalid_reads_leave_destination_unchanged`.
- [ ] Testar imagem ausente, caminho não regular/não abrível, setor inicial fora da imagem, intervalo cruzando EOF, setor final truncado e imagem truncada após enfileirar. Verificar os bytes sentinela de destino e que nenhuma conclusão bem-sucedida seja publicada.
- [ ] Testar contagem zero como no-op explícito (sem I/O nem apagar uma requisição pendente), multiplicação de tamanho/status assinado, faixa guest cruzando 32 MiB, e segunda requisição enquanto já há outra pendente. A segunda requisição não pode apagar silenciosamente a primeira.
- [ ] Preservar assinaturas `Initialize(string_view)`, `QueueRead(uint32_t,uint32_t,uint32_t)` e `ProcessPendingRequests()`. Usar aritmética ampla antes de converter/alocar; rejeitar parâmetros inválidos com exceções específicas, e falhas de I/O com `std::ios_base::failure` ou exceção específica equivalente. O retorno inteiro existente de `QueueRead` permanece zero apenas no sucesso.
- [ ] Validar a faixa antes da alocação e do I/O; ler todo o pedido em buffer temporário, conferir seek e quantidade exata, e só então escrever os dados e publicar a conclusão. Em falha, manter destino e estado de conclusão anteriores à tentativa; preservar a requisição para reset/retry explícito, sem fingir sucesso.
- [ ] Ajustar os testes antigos que aceitavam leitura sem imagem. Atualizar o teste de adapter para usar uma requisição válida/fixture quando necessário. Nenhum teste existente deve ser removido para esconder mudança de contrato.
- [ ] Não alterar o leitor compartilhado do runtime neste commit/tarefa. Se a execução provar um defeito nesse caminho, registrar a evidência e delimitar a correção reproduzível antes de avançar; manter a distinção entre ISO bruto e padding legitimamente exigido por arquivos mapeados.

## Tarefa 4 — validar o boot e documentar o resultado real

- [ ] Build diagnóstico pelo container, sem compilação irrestrita no host:
  `python3 tools/docker_build.py --platform desktop --diagnostics --jobs 4`.
  Esperado: compilação e link de `aoe2_game` sem erros.
- [ ] Executar `python3 tools/validate_desktop.py --run-seconds 30 --interval 10`; guardar log em `build/desktop-validation/run.log`, veredito e capturas em `.playwright-mcp/`.
- [ ] Procurar evidência positiva: verificação aceita; Thread 2 avança; pedido real de `smenu_cd.bin`; ID 6 ativo; despacho da entrada do SMENU; menu renderizado. Cabeçalho escrito ou lookup disponível não prova execução da entrada. Acrescentar diagnóstico limitado no dispatcher somente se necessário, sem alterar o despacho.
- [ ] Inspecionar as imagens. A aprovação visual exige menu reconhecível e execução estável, não só frames diferentes ou processo vivo. Não flexibilizar o validador para converter a tela de loading em PASS.
- [ ] Se houver progresso mensurável em 30 s mas tempo insuficiente, realizar uma única extensão de 120 s (`--interval 20`). Se surgir outro bloqueio, registrá-lo com a última operação bem-sucedida e encerrar este escopo sem sucessivos bypasses.
- [ ] Atualizar os dois documentos de resultados/semântica, incluindo quais caminhos estão cobertos e quais permanecem originais. Revisão independente de contrato/erros, `git diff --check` e checagem dos caminhos de evidência.

## Comandos de verificação

Para a regressão original focada, configurar um diretório separado, sem recompilar todas as unidades do jogo:

```bash
cmake -S . -B build/cdvd-media-tests -G Ninja \
  -DAOE2_BUILD_DECI2_RUNTIME_TESTS=ON -DAOE2_BUILD_CDVD_RUNTIME_TESTS=ON
LC_ALL=C systemd-run --user --scope --quiet \
  -p MemoryMax=8G -p MemorySwapMax=0 -- \
  cmake --build build/cdvd-media-tests --target aoe2_cdvd_media_tests aoe2_cdvd_model_tests -j 4
ctest --test-dir build/cdvd-media-tests -R '^(cdvd_media|cdvd_model)$' --output-on-failure
```

Esperado na tarefa 1: falha comportamental do caso com override, com baseline original aprovado. Após a tarefa 2: ambos aprovados. Compilação/configuração quebrada não conta como RED.

Para o modelo e regressões leves:

```bash
cmake -S . -B build/scmd-smenu-lightweight -G Ninja \
  -DAOE2_BUILD_DESKTOP_PROBE=OFF -DAOE2_BUILD_GAME=OFF
cmake --build build/scmd-smenu-lightweight -j 4
ctest --test-dir build/scmd-smenu-lightweight --output-on-failure
python3 -m unittest discover -s tests
```

Reconstruir os alvos de teste modificados do desktop-probe sob o mesmo limite de recursos antes de executar `ctest --test-dir build/desktop-probe --output-on-failure`; usar `tools/run_desktop_probe_build.py` se for necessário reconstruir o probe completo. Não usar executáveis de teste antigos como evidência de código novo.

## Critérios de aceite e foco da revisão

- Contrato: regressão demonstra o defeito antigo e a preservação do original, incluindo retorno/estrutura e rejeição legítima; disponibilidade 2 não é falsamente tratada como erro.
- Integridade: bytes exatos para 1/múltiplos setores e fronteira final; ausência/short read/overflow/faixa guest inválida não sobrescrevem destino nem publicam sucesso.
- Concorrência lógica: zero setores ou um novo pedido não descartam um pedido pendente; chamadas repetidas não dependem de valor residual em `v0`.
- Integração: função original pode suspender/retomar via scheduler; testes sentinela não substituem a prova do chamador original.
- Resultado final separado: (a) contrato corrigido; (b) leitor estrito; (c) SMENU realmente alcançado; (d) menu visível. Somente (d) satisfaz o marco visual. Qualquer item não comprovado deve ser relatado como pendente, nunca como sucesso implícito.
