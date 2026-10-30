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

ARG BUILD_UID=1000
ARG BUILD_GID=1000
RUN (getent group "${BUILD_GID}" || groupadd -g "${BUILD_GID}" builder) \
 && (getent passwd "${BUILD_UID}" || useradd -m -u "${BUILD_UID}" -g "${BUILD_GID}" builder)
USER ${BUILD_UID}:${BUILD_GID}
WORKDIR /src

HEALTHCHECK NONE
