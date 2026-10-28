# Container for running Nintendo Switch validation with Ryujinx.Headless.SDL2
# under Xvfb with Mesa LLVMpipe software rendering.

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive \
    TZ=America/Sao_Paulo \
    LC_ALL=C.UTF-8

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    tar \
    xvfb \
    x11-apps \
    imagemagick \
    libgl1-mesa-dri \
    libgl1-mesa-glx \
    libglu1-mesa \
    libvulkan1 \
    mesa-vulkan-drivers \
    libsdl2-2.0-0 \
    tzdata \
    procps \
 && rm -rf /var/lib/apt/lists/*

# Install Ryujinx release archive or user-provided binary
ARG RYUJINX_VERSION=1.3.3
ARG RYUJINX_URL=https://git.ryujinx.app/projects/Ryubing/releases/download/${RYUJINX_VERSION}/ryujinx-${RYUJINX_VERSION}-linux_x64.tar.gz

RUN mkdir -p /opt/ryujinx \
 && (curl -fsSL --retry 3 "${RYUJINX_URL}" -o /tmp/ryujinx.tar.gz \
     && tar -xzf /tmp/ryujinx.tar.gz -C /opt/ryujinx --strip-components=1 \
     && rm -f /tmp/ryujinx.tar.gz \
     || echo "Ryujinx download will be completed or mounted at test time")

ENV PATH="/opt/ryujinx:${PATH}"

ARG BUILD_UID=1000
ARG BUILD_GID=1000
RUN (getent group "${BUILD_GID}" || groupadd -g "${BUILD_GID}" builder) \
 && (getent passwd "${BUILD_UID}" || useradd -m -u "${BUILD_UID}" -g "${BUILD_GID}" builder)

USER ${BUILD_UID}:${BUILD_GID}
WORKDIR /src

HEALTHCHECK NONE
