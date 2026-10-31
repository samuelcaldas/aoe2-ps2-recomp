# Nintendo Switch homebrew build environment (devkitPro only ever runs here, never
# on the host). Graphics: OpenGL ES via devkitPro's Mesa; windowing and input via
# SDL2, which raylib uses through its SDL platform backend.
FROM devkitpro/devkita64:latest

ENV TZ=America/Sao_Paulo \
    LC_ALL=C.UTF-8

RUN dkp-pacman -Syu --noconfirm --needed \
      switch-sdl2 switch-mesa switch-libdrm_nouveau switch-glad switch-zlib \
 && dkp-pacman -Scc --noconfirm

RUN apt-get update \
 && apt-get install -y --no-install-recommends cmake ninja-build lld python3 git ca-certificates tzdata \
 && rm -rf /var/lib/apt/lists/*

# Provide a devkitA64-compatible ld.lld wrapper and linker script so -fuse-ld=lld
# translates libnx switch.specs flags (--spare-dynamic-tags, --nx-module-name,
# --require-defined, -z pack-relative-relocs) and adds the PT_TLS and PT_GNU_EH_FRAME phdrs required by lld.
RUN python3 -c 's = open("/opt/devkitpro/libnx/switch.ld").read(); \
s = s.replace("dyn    PT_DYNAMIC;", "dyn    PT_DYNAMIC;\n\ttls    PT_TLS;\n\teh_frame_hdr PT_GNU_EH_FRAME;"); \
s = s.replace(".eh_frame_hdr      : { *(.eh_frame_hdr) *(.eh_frame_entry .eh_frame_entry.*) } :rodata", ".eh_frame_hdr      : { *(.eh_frame_hdr) *(.eh_frame_entry .eh_frame_entry.*) } :rodata :eh_frame_hdr"); \
s = s.replace(".tdata :\n\t{\n\t\t*(.tdata .tdata.* .gnu.linkonce.td.*)\n\t\t. = ALIGN(8);\n\t} :data", ".tdata :\n\t{\n\t\t*(.tdata .tdata.* .gnu.linkonce.td.*)\n\t\t. = ALIGN(8);\n\t} :data :tls"); \
s = s.replace(".tbss :\n\t{\n\t\t*(.tbss .tbss.* .gnu.linkonce.tb.*)\n\t\t*(.tcommon)\n\t\t. = ALIGN(8);\n\t} :data", ".tbss :\n\t{\n\t\t*(.tbss .tbss.* .gnu.linkonce.tb.*)\n\t\t*(.tcommon)\n\t\t. = ALIGN(8);\n\t} :data :tls"); \
open("/opt/devkitpro/libnx/switch_lld.ld", "w").write(s)' \
 && printf '%s\n' \
    '#!/usr/bin/env bash' \
    'filtered=()' \
    'skip_next=0' \
    'args=("$@")' \
    'for ((i=0; i<${#args[@]}; i++)); do' \
    '    if (( skip_next )); then skip_next=0; continue; fi' \
    '    arg="${args[$i]}"' \
    '    case "$arg" in' \
    '        --spare-dynamic-tags=*) ;;' \
    '        --nx-module-name) ;;' \
    '        --require-defined=*) filtered+=("--undefined=${arg#--require-defined=}") ;;' \
    '        /opt/devkitpro/libnx/switch.ld) filtered+=("/opt/devkitpro/libnx/switch_lld.ld") ;;' \
    '        -z)' \
    '            next="${args[$((i+1))]:-}"' \
    '            if [[ "$next" == "nodynamic-undefined-weak" ]]; then skip_next=1' \
    '            elif [[ "$next" == "pack-relative-relocs" ]]; then filtered+=("--pack-dyn-relocs=relr"); skip_next=1' \
    '            else filtered+=("-z"); fi' \
    '            ;;' \
    '        *) filtered+=("$arg") ;;' \
    '    esac' \
    'done' \
    'exec /usr/bin/ld.lld "${filtered[@]}"' \
    > /opt/devkitpro/devkitA64/bin/ld.lld \
 && chmod +x /opt/devkitpro/devkitA64/bin/ld.lld \
 && ln -sf /opt/devkitpro/devkitA64/bin/ld.lld /opt/devkitpro/devkitA64/aarch64-none-elf/bin/ld.lld

ARG BUILD_UID=1000
ARG BUILD_GID=1000
RUN (getent group "${BUILD_GID}" || groupadd -g "${BUILD_GID}" builder) \
 && (getent passwd "${BUILD_UID}" || useradd -m -u "${BUILD_UID}" -g "${BUILD_GID}" builder)
USER ${BUILD_UID}:${BUILD_GID}
WORKDIR /src

HEALTHCHECK NONE
