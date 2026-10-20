# Prompt para o agy: terminar o build do desktop probe (j3, com proteção de memória)

Copie o bloco abaixo e entregue ao `agy-worker`.

````text
Você é um executor. Siga os passos EXATAMENTE, na ordem, sem improvisar.

Repositório: /home/samuelcaldas/source/repos/aoe2-ps2-recomp
Objetivo: terminar de compilar e tentar linkar o alvo `desktop_runtime_probe`
usando UM script que já existe e já protege a memória do sistema.
Você NÃO deve consertar nada. Você só roda o script, lê o resultado e relata.

## Regras que você nunca pode quebrar
1. Não edite NENHUM arquivo. Nem CMakeLists.txt, nem código gerado, nem
   third_party/, nem tools/, nem tests/.
2. Não rode `ninja`, `cmake --build` ou `make` diretamente. Só o script abaixo.
3. Não use `sudo`, `kill -9`, `pkill`, nem `systemctl` por conta própria.
4. Não rode o binário `desktop_runtime_probe`. Não execute código do jogo.
5. Não adicione stubs, patches ou remova arquivos gerados para o link passar.
6. Não faça `git add`, `git commit` nem `git push`.
7. Não copie para nenhum arquivo versionado nomes de funções, endereços
   hexadecimais ou trechos de código vindos dos logs. Logs ficam só em build/.
8. Não interrompa com Ctrl-C a menos que o passo 4 mande.

## Passo 1: conferir o estado (somente leitura)
Rode, nesta ordem, e anote o resultado de cada um:
  cd /home/samuelcaldas/source/repos/aoe2-ps2-recomp
  git status --short
  free -h
  pgrep -a 'ninja|cc1plus' || echo "nenhum build rodando"
Se aparecer `ninja` ou `cc1plus` rodando: PARE e relate. Não mate nada.
Se "disponível" em `free -h` for menor que 12Gi: PARE e relate.

## Passo 2: ensaio do script (não compila nada)
  python3 tools/run_desktop_probe_build.py --dry-run
Esperado: termina com exit 0 e imprime uma linha "DRY RUN command: ...".
Se terminar com exit 2 ("PREFLIGHT FAILED"), PARE e relate a mensagem exata.

## Passo 3: rodar o build de verdade, em segundo plano
O build pode levar muito tempo. Rode em segundo plano e NÃO use timeout curto:
  python3 tools/run_desktop_probe_build.py
(O script usa 3 jobs, limita a memória a 18 GiB sem swap e aborta se a
memória livre do sistema cair abaixo de 3 GiB. Ele é idempotente: pode ser
rodado de novo sem estragar nada, e continua de onde parou.)

## Passo 4: acompanhar sem atrapalhar
Enquanto roda, a cada 2 a 3 minutos, e só leitura:
  tail -n 3 build/desktop-probe/raw_compile_j3.log | cut -c1-200
  free -h | sed -n 2p
Não rode mais nada pesado ao mesmo tempo.
Só interrompa (Ctrl-C no processo do script, UMA vez) se "disponível" em
`free -h` ficar abaixo de 2Gi E o script não tiver parado sozinho em 1 minuto.
Se interromper, vá direto ao passo 5 com o código de saída que o script der.

## Passo 5: ler o código de saída e decidir

| Exit | Significado | O que fazer |
|------|-------------|-------------|
| 0 | Build e link terminaram | Vá ao passo 6 (caso SUCESSO) |
| 2 | Pré-requisito falhou | Relate a mensagem exata. Não tente consertar. |
| 3 | Guarda de memória parou o build | Rode UMA vez: `python3 tools/run_desktop_probe_build.py --jobs 2` e volte ao passo 5. Se der 3 de novo, relate e pare. |
| 4 | Erro de compilação ou link | O script já gerou build/desktop-probe/classified_report_j3.json. Vá ao passo 6 (caso FALHA). |
| 5 | O classificador falhou | Relate a saída do script. Não tente consertar. |

## Passo 6: relatório final
SUCESSO (exit 0):
  ls -la build/desktop-probe/desktop_runtime_probe
  sha256sum build/desktop-probe/desktop_runtime_probe
  git status --short
Relate: o binário existe, tamanho, sha256, e que nada novo ficou rastreado.
NÃO execute o binário.

FALHA (exit 4):
  python3 -c "import json;d=json.load(open('build/desktop-probe/classified_report_j3.json'));print(json.dumps(d,indent=2)[:4000])"
  grep -c 'FAILED:' build/desktop-probe/raw_compile_j3.log
  git status --short
Relate: contagem por categoria do relatório, quantos passos falharam, e a
categoria do PRIMEIRO erro. Se o relatório trouxer nomes de funções ou
endereços, não os repita; diga só "contém identificadores do jogo".

## Formato da sua resposta final (obrigatório, curto)
- Exit code do script:
- Comandos que você rodou (lista):
- Resultado (SUCESSO / FALHA / PARADO PELA GUARDA / PRÉ-REQUISITO):
- Memória disponível no fim (`free -h`):
- Categoria do primeiro bloqueio (se houver):
- `git status --short` no fim (deve mostrar só os arquivos que já estavam
  pendentes antes: CMakeLists.txt, docs/, tests/, tools/):
- Qualquer coisa inesperada:
Não proponha correções. Quem decide o próximo passo é o orquestrador.
````
