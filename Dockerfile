# Global build arguments (must be declared before the first FROM to be usable in
# the second stage's FROM line).
#
# Build either architecture from this single Dockerfile:
#   docker build --build-arg ARCH=aarch64 --tag mediamtx-aarch64 .
#   docker build --build-arg ARCH=armv7hf --tag mediamtx-armv7hf .
ARG ARCH=aarch64
ARG VERSION=12.10.0
ARG UBUNTU_VERSION=24.04
ARG REPO=axisecp
ARG SDK=acap-native-sdk
# Keep in step with app/manifest.json: CI rewrites this via apply-version.sh, but
# a local build uses this value, and an older mediamtx rejects newer config keys.
ARG MEDIAMTX_VERSION=1.20.1
# Upstream MediaMTX release architecture; derived from ARCH when left empty.
ARG MEDIAMTX_ARCH=

# --- Stage 1: download and verify the MediaMTX release binary ---
FROM ubuntu:24.04 AS fetch
ARG ARCH
ARG MEDIAMTX_VERSION
ARG MEDIAMTX_ARCH
RUN apt-get update \
 && apt-get install -y --no-install-recommends curl ca-certificates \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /dl
RUN set -eux; \
    arch="${MEDIAMTX_ARCH}"; \
    if [ -z "$arch" ]; then \
        case "$ARCH" in \
            aarch64) arch=linux_arm64 ;; \
            armv7hf) arch=linux_armv7 ;; \
            *) echo "unsupported ARCH '$ARCH' (use aarch64 or armv7hf)" >&2; exit 1 ;; \
        esac; \
    fi; \
    base="https://github.com/bluenviron/mediamtx/releases/download/v${MEDIAMTX_VERSION}"; \
    file="mediamtx_v${MEDIAMTX_VERSION}_${arch}.tar.gz"; \
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
# Kept outside /opt/app so it never lands in the .eap. strip does not move code,
# so this resolves addresses from a stripped-binary crash via addr2line.
RUN mkdir -p /opt/debug && cp /opt/app/lib/mediamtx /opt/debug/mediamtx.unstripped
# Upstream ships mediamtx unstripped; stripping saves ~15 MB (25% of the binary).
RUN find . -name .DS_Store -delete && chmod +x /opt/app/lib/mediamtx && . /opt/axis/acapsdk/environment-setup* && \
    "${STRIP:?SDK environment did not set STRIP}" /opt/app/lib/mediamtx && \
    acap-build ./ -a mediamtx.defaults.yml -a config.cgi
