# Plano: catálogo e extração local de assets HD (UI widescreen da AoE II 2013 HD primeiro; DE depois)

## Contexto

O projeto recompila o AoE II PS2 (PAL). `docs/architecture.md` §7.3 já prevê a edição 2013 (HD) como camada de override: assets PS2 como base, HD quando houver correspondência verificada. Falta o mecanismo que importa a instalação HD do usuário e extrai os assets localmente, com a proveniência do `AOK.ELF` (nada é commitado).

**Prioridade do usuário:** a UI HD widescreen da AoE II 2013 HD. Suporte à Definitive Edition (DE) entra só se for compatível e barato.

**Restrição:** o agente `@aoe-recomp` tem prioridade. Este trabalho só **cria arquivos novos** e roda numa worktree própria. Não edita `core/target.py`, `CMakeLists.txt`, `runtime/`, `tools/run_*`, nem o TOML e o manifesto de geração. A única edição em arquivo existente é um link em `docs/architecture.md` §7.3, e só se o arquivo estiver limpo no `git status`.

## O que a pesquisa mostrou (openage, usado só como referência)

Fontes lidas: documentação do openage em `doc/media/` (DRS, SLP, SMX, SLD) e `cfg/converter/games/game_editions.toml` com os `version_hashes.toml` de cada edição. A busca web direta falhou com erro 503 do gateway, então tudo veio pela API do GitHub. Outros projetos (SLP Studio, AoE2Wide, genieutils) **não foram verificados**.

1. **O openage separa detecção, mapeamento de mídia e decodificação.** Cada edição é um perfil declarativo com três partes:
   - arquivos-sentinela com hash (por exemplo `AoK HD.exe` e `empires2_x1_p1.dat` para a HD, `AoE2DE_s.exe` e `empires2_x2_p1.dat` para a DE) que identificam a edição e a versão;
   - um mapa de categorias de mídia para caminhos (`graphics`, `interface`, `palettes`, `terrain`, `sounds`, `gamedata`, `datfile`);
   - decodificadores por formato.
   O hash usa SHA3-256 e a versão do jogo vem do hash do arquivo-sentinela.
2. **Na HD e na DE os "DRS" são pastas de arquivos soltos**, e não arquivos `.drs`. Para a HD: `resources/_common/drs/{graphics,interface,gamedata_x1,sounds}/`, `resources/_common/terrain/textures/` e `resources/_common/dat/empires2_x1_p1.dat`. Para a DE: `resources/_common/drs/{graphics,interface,gamedata_x2}/`, `resources/_common/palettes/` e `empires2_x2_p1.dat`. O formato de contêiner DRS (cabeçalho, tabelas por tipo, entradas id/offset/tamanho) só vale para AoK/AoC/PS2-like e para a AoE II original. **O plano anterior assumia `.drs` e estava errado para a HD.**
3. **Formatos.** A HD usa SLP versão `2.0N`, igual à AoE II original (o mesmo decodificador serve para as duas). A DE moderna usa SLD (blocos DXT1/BC4 com camadas de sombra, máscara de dano e cor de jogador). SMX é o formato da DE inicial. Cada formato tem especificação pública do openage. O nome do arquivo solto é o id do asset.
4. **A interface é uma categoria própria** (`interface`) e também fornece as paletas na HD. Se a HD guarda variantes de UI por largura ou peças de preenchimento para widescreen é **desconhecido**: a documentação não diz, e `architecture.md` §7.5 já lista isso como pergunta aberta. Só a instalação real responde.

Licenças: o openage é GPL-3.0. Vamos reimplementar a partir das especificações de formato e usar os caminhos como fatos sobre o jogo, sem copiar código. Registrar isso em `docs/hd-assets.md`.

## Paralelo com o PS2

| Aspecto | PC (HD/DE) | PS2 (SLES-50282) |
|---|---|---|
| Documentação | Pública e extensa | Praticamente nenhuma |
| Contêiner | Pastas de arquivos soltos por id | **Desconhecido.** Só o `AOK.ELF` foi importado; os dados do disco ainda não |
| Sprites | SLP 2.0N (HD), SLD (DE) | **Desconhecido** |
| Identificação | Hash do arquivo-sentinela | SHA-1 do ELF já existe |
| Correlação | ids de gráfico estáveis desde a AoE II 1999 | Hipótese: o port PS2 deriva dos dados PC do AoK, então os ids podem coincidir. **Não verificada** |

Consequências para o design:

- Usar um modelo de **catálogo independente de edição**: perfil → lista de ativos (categoria, id, formato detectado por assinatura, tamanho, SHA-1). O mesmo modelo cataloga depois o disco PS2, e a correlação PS2 ↔ HD passa a ser uma junção de catálogos locais.
- Detectar formato por assinatura de bytes (`2.0N`, `SLDX`, `SMPX`, DDS), nunca só por extensão. Isso é o que permite descobrir o formato PS2 sem documentação.
- A correlação continua fora do escopo. O que entra agora é só o insumo dela.
- Pré-requisito do lado PS2, a decidir depois: importar o conteúdo do disco, já que o `import_owned_game.py` cobre só o ELF.

## Escopo por fase

**Fase 1 (este plano): HD 2013, UI primeiro, sem decodificar imagens.**
Importar, catalogar, verificar. Prioridade de categorias: `interface`, `palettes`, depois `graphics`, `terrain`, `gamedata`, `datfile`. Um comando de inventário gera o relatório local que responde às perguntas abertas da §7.5 (há variantes de UI por resolução? há peças de preenchimento?).

**Fase 2: decodificação da UI.** Decodificador SLP 2.0N + paleta, exportação para PNG/atlas RGBA em `game/`, para o `IRenderer` consumir no futuro. Só depois de o inventário mostrar o que a UI widescreen realmente é.

**Fase 3: DE (opcional).** Perfil `AOE2DE` e decodificadores SLD/DXT na mesma interface. É compatível por construção, porque o perfil e o catálogo já são independentes de edição. Fica fora até a UI HD funcionar.

## Design da fase 1

Python 3 stdlib, falha cedo, no padrão de `tools/import_owned_game.py` (`REPO_ROOT` + `sys.path`, SHA em blocos de 64 KiB, idempotente, nunca imprime conteúdo binário).

Arquivos novos:

- `core/hd_source.py`: perfis imutáveis `EditionProfile` (`HD2013`, depois `DE2`): nome, sentinelas, mapa categoria → caminho relativo. Separado de `core/target.py`.
- `tools/hd_assets/`: módulos curtos (Object Calisthenics).
  - `edition_detector.py`: valida a pasta de origem contra o perfil e identifica a edição pelos sentinelas. Erro específico se faltar algo.
  - `format_sniffer.py`: assinatura de bytes → formato.
  - `asset_catalog.py`: varre as categorias e produz o catálogo (categoria, id numérico do nome, formato, tamanho, SHA-1). Coleção de primeira classe.
  - `importer.py`: copia as categorias escolhidas para `game/hd-edition/source/`, verificando antes e depois.
  - `manifests.py`: manifesto local completo e manifesto rastreável resumido.
- `tools/import_hd_assets.py --source <instalação> [--categories interface,palettes]`: valida, copia, cataloga.
- `tools/inventory_hd_assets.py`: relatório local de `game/hd-edition/` (contagens por categoria e formato, faixas de ids, dimensões lidas dos cabeçalhos SLP), usado para responder à §7.5. Não é rastreado.
- `tools/verify_hd_assets.py`: reconfere hashes, catálogo e manifestos; sai com código diferente de zero em qualquer divergência.
- `tests/test_hd_assets.py` (unittest, tmpdir): árvore sintética com arquivos mínimos e assinaturas falsas. Casos: origem ausente, perfil não reconhecido, categoria faltando, arquivo vazio, 0/1/muitos ativos, formato desconhecido, reimportação idempotente, conflito, corrupção de um byte detectada. Teste com instalação real é pulado quando o caminho não existe.
- `docs/hd-assets.md`: comandos, layout em disco, proveniência, perfis, achados do inventário, licenças.

Saídas:

- Local e ignorado (`game/` já está no `.gitignore`): `game/hd-edition/{source,catalog.json,inventory.txt}`.
- Rastreável `hd_assets_manifest.json`: só edição, versão detectada, contagens por categoria e formato, SHA-256 agregado por categoria e versão da ferramenta. Sem nomes de arquivos, ids, caminhos nem dimensões. O verificador checa que esse arquivo não contém campos fora desse esquema.

## Execução (após aprovação)

1. Aguardar o caminho da instalação HD (o usuário está baixando). Até lá, implementar com árvore sintética nos testes.
2. `git fetch && git rebase origin/main`, conferir `git status`. Se o outro agente tiver edições pendentes em arquivos que este plano toca, parar e avisar.
3. `git worktree add .worktrees/hd-assets -b hd-assets`.
4. TDD: testes, depois `edition_detector` → `format_sniffer` → `asset_catalog` → `importer` → `manifests` → CLIs.
5. Rodar com a instalação real; ajustar o perfil `HD2013` ao layout observado (os caminhos acima vêm do openage e podem ter mudado com o patch atual); registrar achados em `docs/hd-assets.md`.
6. Commits lógicos, rebase, merge `--no-ff` em `main`, testes em `main`, remover worktree e branch.

## Verificação

```bash
python3 -m unittest tests.test_hd_assets
python3 -m unittest discover -s tests
python3 tools/import_hd_assets.py --source "<instalação HD>" --categories interface,palettes
python3 tools/inventory_hd_assets.py
python3 tools/verify_hd_assets.py
git status --short                       # só arquivos novos; nada de game/
git check-ignore game/hd-edition         # ignorado
git diff --stat main...hd-assets         # nenhum arquivo do outro agente tocado
```

Aceitação: reimportação sem mudanças; corromper um byte da cópia faz o verificador falhar; o manifesto rastreável não vaza nomes nem ids; o inventário responde se a HD tem variantes de UI por resolução.

## Riscos e incógnitas

- O layout real pode diferir do openage (patch novo da HD). Mitigação: perfil declarativo com sentinelas e erro claro.
- Formato do disco PS2 desconhecido, e a hipótese de ids coincidentes não foi verificada. Mitigação: catálogo por assinatura e correlação tratada como etapa própria.
- A UI widescreen pode não existir como assets prontos (a HD pode compor a UI por código). O inventário da fase 1 decide se a fase 2 extrai peças ou se o HUD precisa de outra abordagem (`architecture.md` §7.4, opção 2).
