ARG ARCH=aarch64
ARG VERSION=12.9.0
ARG UBUNTU_VERSION=24.04
ARG REPO=axisecp
ARG SDK=acap-native-sdk

FROM ${REPO}/${SDK}:${VERSION}-${ARCH}-ubuntu${UBUNTU_VERSION}

# Building the ACAP application
COPY ./app /opt/app/
WORKDIR /opt/app
RUN find . -name .DS_Store -delete && chmod +x /opt/app/lib/mediamtx && . /opt/axis/acapsdk/environment-setup* && acap-build ./ -a mediamtx.defaults.yml -a config.cgi
