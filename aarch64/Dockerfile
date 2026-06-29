# Global build arguments (must be declared before the first FROM to be usable in
# the second stage's FROM line).
ARG ARCH=aarch64
ARG VERSION=12.9.0
ARG UBUNTU_VERSION=24.04
ARG REPO=axisecp
ARG SDK=acap-native-sdk
ARG MEDIAMTX_VERSION=1.19.2
ARG MEDIAMTX_ARCH=linux_arm64

# --- Stage 1: download and verify the MediaMTX release binary ---
FROM ubuntu:24.04 AS fetch
ARG MEDIAMTX_VERSION
ARG MEDIAMTX_ARCH
RUN apt-get update \
 && apt-get install -y --no-install-recommends curl ca-certificates \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /dl
RUN set -eux; \
    base="https://github.com/bluenviron/mediamtx/releases/download/v${MEDIAMTX_VERSION}"; \
    file="mediamtx_v${MEDIAMTX_VERSION}_${MEDIAMTX_ARCH}.tar.gz"; \
    curl -fsSL -o "$file" "$base/$file"; \
    curl -fsSL -o checksums.sha256 "$base/checksums.sha256"; \
    grep -F "$file" checksums.sha256 | sha256sum --check -; \
    tar -xzf "$file" mediamtx; \
    chmod +x mediamtx

# --- Stage 2: build the ACAP application ---
FROM ${REPO}/${SDK}:${VERSION}-${ARCH}-ubuntu${UBUNTU_VERSION}

# Bring in the verified MediaMTX binary downloaded in the fetch stage.
COPY ./app /opt/app/
COPY --from=fetch /dl/mediamtx /opt/app/lib/mediamtx
WORKDIR /opt/app
RUN find . -name .DS_Store -delete && chmod +x /opt/app/lib/mediamtx && . /opt/axis/acapsdk/environment-setup* && acap-build ./ -a mediamtx.defaults.yml -a config.cgi
