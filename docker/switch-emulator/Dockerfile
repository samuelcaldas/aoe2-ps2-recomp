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
    x11vnc \
    novnc \
    websockify \
    libgl1 \
    libgl1-mesa-dri \
    libglx-mesa0 \
    libglu1-mesa \
    libvulkan1 \
    mesa-vulkan-drivers \
    libsdl2-2.0-0 \
    tzdata \
    procps \
    xdotool \
 && rm -rf /var/lib/apt/lists/*

# Install Ryujinx release archive or user-provided binary
ARG RYUJINX_VERSION=1.3.31
ARG RYUJINX_URL=https://github.com/ADEMOLA200/Ryujinx-Canary-Builds/releases/download/${RYUJINX_VERSION}/ryujinx-canary-${RYUJINX_VERSION}-linux_x64.tar.gz

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

EXPOSE 6080 5900

HEALTHCHECK NONE
