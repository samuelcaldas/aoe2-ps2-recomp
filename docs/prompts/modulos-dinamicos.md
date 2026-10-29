# TASK: Otimizar Linkagem do Nintendo Switch (Linker Moderno + Modularização de Overlays)

Você deve aplicar duas melhorias de desempenho na pipeline de compilação e linkagem do Nintendo Switch (devkitA64/CMake) no repositório `aoe2-ps2-recomp`:
1. Configurar o linker LLVM `lld` para substituir o GNU `ld` lento.
2. Desacoplar os 9 overlays recheados do executável principal (`aoe2_game.elf`), transformando-os em módulos independentes/separados para reduzir o pico de memória e tempo de linkagem.

Siga rigorosamente as instruções abaixo sem alterar a semântica da execução do jogo.

---

## FASE 1: Habilitar Linker Moderno (`lld`) e Otimizar Metadados

### Arquivo-alvo: `cmake/Aoe2Recompiled.cmake` e/ou `CMakeLists.txt`
1. **Adicionar flags de linker para NINTENDO_SWITCH**:
    No alvo `aoe2_game`, configure o uso de `lld` quando compilando para Switch com devkitA64:
    ```cmake
    if(NINTENDO_SWITCH)
        # Utiliza lld caso disponível no toolchain devkitA64
        target_link_options(aoe2_game PRIVATE -fuse-ld=lld)
        # Oculta visibilidade padrão dos símbolos gerados para não poluir a tabela dinâmica
        target_compile_options(aoe2_generated_game_code PRIVATE -fvisibility=hidden)
    endif()
    ```
2. Aumentar o tamanho do Unity Batch:
Em cmake/Aoe2Recompiled.cmake, localize a definição de AOE2_GENERATED_UNITY_BATCH (atualmente 16).
    • Ajuste o default para Switch ou configure para 64 para reduzir o número de objetos .o de ~3664 para menos de 950.
    • Isso diminui o número de file descriptors e seções que o linker precisa resolver em paralelo.


### Critério de Aceite da Fase 1:

• Execute dentro do container:
docker run --rm -v $(pwd):/src -w /src/build/docker-switch aoe2-build-switch ninja -t commands aoe2_game | grep -E "fuse-ld=lld"
• O comando de linkagem deve conter -fuse-ld=lld.
──────
## FASE 2: Desacoplar os Overlays do Binário Principal

### Contexto de Arquitetura:

Atualmente, todos os 9 overlays (OVERLAY_00 até OVERLAY_08 em generated/overlays/) são vinculados diretamente dentro do executável estático aoe2_game.elf através da biblioteca aoe2_recompiled_overlays no CMakeLists.txt.
O executável base do jogo (AOK.ELF) só precisa do código base (generated/SLES-50282/) durante o boot; os overlays só são carregados dinamicamente em runtime quando uma partida ou menu específico é acionado.

### Passos de Implementação:

1. Modificar CMakeLists.txt:
    • Localize o target aoe2_recompiled_overlays e a diretiva onde aoe2_game dá target_link_libraries(aoe2_game PRIVATE ...
    aoe2_recompiled_overlays).
    • Crie uma opção AOE2_MODULAR_OVERLAYS (default ON para NINTENDO_SWITCH).
    • Quando AOE2_MODULAR_OVERLAYS estiver ativo:
        • Não vincule aoe2_recompiled_overlays dentro de aoe2_game.elf.
        • Compile cada overlay como uma biblioteca compartilhada separada (add_library(aoe2_overlay_0X SHARED ...) ou módulo estático de payload)
        gerando arquivos .so colocados na pasta romfs_aoe2/overlays/ para empacotamento com o elf2nro.

2. Atualizar o Dispatcher de Overlay no Runtime (game_hooks ou PS2Runtime):
    • Localize onde o overlay é registrado na inicialização (register_overlays()).
    • No modo modular, em vez de registrar todos os ponteiros de função de todos os overlays na inicialização do ELF, o runtime deve carregar o módulo do overlay sob demanda (dlopen ou tabela de relocação local) apenas quando a syscall de carregamento de arquivo do CDVD (sceCdRead / LoadExecPS2) carregar o respectivo segmento do overlay na RAM guest.

──────
## REGRAS OBRIGATÓRIAS

1. Não quebre o build desktop: Verifique se o desktop continua compilando normalmente com cmake -B build -G Ninja && cmake --build build --target aoe2_smoke_test.
2. Não use -j irrestrito no link: O link do Switch deve respeitar o limite de memória da máquina host.
3. Commit limpo: Faça alterações pontuais em commits lógicos e claros (ex.: build: use lld and increase unity batch size for switch, arch: decouple overlays into loadable modules).
