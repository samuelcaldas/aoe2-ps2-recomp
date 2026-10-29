# Desktop Linux x86-64 build environment for the recompiled game.
# Matches the host distribution so binaries built here also run on the host.
FROM ubuntu:26.04

ENV DEBIAN_FRONTEND=noninteractive \
    TZ=America/Sao_Paulo \
    LC_ALL=C.UTF-8

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential cmake ninja-build git ca-certificates pkg-config python3 tzdata \
      libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxext-dev \
      libgl1-mesa-dev libglu1-mesa-dev libwayland-dev libxkbcommon-dev \
      libasound2-dev libpulse-dev \
      libavcodec-dev libavformat-dev libavutil-dev libswresample-dev libswscale-dev \
      xvfb x11-apps imagemagick \
 && rm -rf /var/lib/apt/lists/*

ARG BUILD_UID=1000
ARG BUILD_GID=1000
RUN (getent group "${BUILD_GID}" || groupadd -g "${BUILD_GID}" builder) \
 && (getent passwd "${BUILD_UID}" || useradd -m -u "${BUILD_UID}" -g "${BUILD_GID}" builder)
USER ${BUILD_UID}:${BUILD_GID}
WORKDIR /src

HEALTHCHECK NONE
