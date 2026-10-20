# Android build and validation environment: SDK, NDK, CMake, Gradle, and an
# x86_64 emulator image (needs --device /dev/kvm) for running the APK headless.
FROM ubuntu:26.04

ENV DEBIAN_FRONTEND=noninteractive \
    TZ=America/Sao_Paulo \
    LC_ALL=C.UTF-8 \
    ANDROID_HOME=/opt/android-sdk \
    ANDROID_SDK_ROOT=/opt/android-sdk \
    GRADLE_HOME=/opt/gradle

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      openjdk-21-jdk-headless wget unzip ca-certificates git python3 tzdata ninja-build \
      libpulse0 libgl1 libnss3 libxcomposite1 libxcursor1 libxi6 libxtst6 libasound2t64 libxkbfile1 \
 && rm -rf /var/lib/apt/lists/*

ARG CMDLINE_TOOLS_ZIP=commandlinetools-linux-11076708_latest.zip
RUN mkdir -p "${ANDROID_HOME}/cmdline-tools" \
 && wget -q "https://dl.google.com/android/repository/${CMDLINE_TOOLS_ZIP}" -O /tmp/cmdline-tools.zip \
 && unzip -q /tmp/cmdline-tools.zip -d "${ANDROID_HOME}/cmdline-tools" \
 && mv "${ANDROID_HOME}/cmdline-tools/cmdline-tools" "${ANDROID_HOME}/cmdline-tools/latest" \
 && rm /tmp/cmdline-tools.zip

ENV PATH="${ANDROID_HOME}/cmdline-tools/latest/bin:${ANDROID_HOME}/platform-tools:${ANDROID_HOME}/emulator:${GRADLE_HOME}/bin:${PATH}"

RUN yes | sdkmanager --licenses > /dev/null \
 && sdkmanager --install \
      "platform-tools" "platforms;android-34" "build-tools;34.0.0" \
      "ndk;28.2.13676358" "cmake;3.22.1" \
      "emulator" "system-images;android-34;default;x86_64" > /dev/null

ARG GRADLE_VERSION=8.9
RUN wget -q "https://services.gradle.org/distributions/gradle-${GRADLE_VERSION}-bin.zip" -O /tmp/gradle.zip \
 && unzip -q /tmp/gradle.zip -d /opt \
 && mv "/opt/gradle-${GRADLE_VERSION}" "${GRADLE_HOME}" \
 && rm /tmp/gradle.zip

ARG BUILD_UID=1000
ARG BUILD_GID=1000
RUN (getent group "${BUILD_GID}" || groupadd -g "${BUILD_GID}" builder) \
 && (getent passwd "${BUILD_UID}" || useradd -m -u "${BUILD_UID}" -g "${BUILD_GID}" builder) \
 && (getent group kvm || groupadd -r kvm) \
 && usermod -aG kvm "$(getent passwd "${BUILD_UID}" | cut -d: -f1)" \
 && chown -R "${BUILD_UID}:${BUILD_GID}" "${ANDROID_HOME}"
USER ${BUILD_UID}:${BUILD_GID}
WORKDIR /src

HEALTHCHECK NONE
