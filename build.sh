#!/usr/bin/env sh
set -eu

REPO_ROOT=$(cd -P "$(dirname "$0")" && pwd)

# Auto-detect container runtime.
# Prefer docker when the daemon is reachable; fall back to podman.
if [ -z "${RUNTIME:-}" ]; then
	if command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1; then
		RUNTIME=docker
	elif command -v podman >/dev/null 2>&1; then
		RUNTIME=podman
	elif command -v docker >/dev/null 2>&1; then
		# docker exists but daemon not running — let it fail with a clear error
		RUNTIME=docker
	else
		echo 'Error: neither docker nor podman found in PATH' >&2
		exit 1
	fi
fi
echo "==> Using container runtime: ${RUNTIME}"

# Optional: override the bundled MediaMTX release (without the leading 'v').
# Leave unset to use the default pinned in the Dockerfile.
MEDIAMTX_VERSION="${MEDIAMTX_VERSION:-}"

# Remove any previously built .eap files so only the current build remains.
echo '==> Cleaning old .eap files...'
rm -f "${REPO_ROOT}"/*.eap

# build_arch <arch>
# Builds the image for one architecture, then copies the generated .eap
# (produced under /opt/app inside the image) out via a temporary container.
# The full SDK image is the final stage, so BuildKit --output cannot be used
# to extract a single file the way the netstack ACAPs do.
build_arch() {
	ARCH=$1
	echo "==> Building .eap package for ${ARCH}..."

	TAG="mediamtx-acap-build-${ARCH}-$$"
	set -- --build-arg ARCH="$ARCH"
	if [ -n "$MEDIAMTX_VERSION" ]; then
		set -- "$@" --build-arg MEDIAMTX_VERSION="$MEDIAMTX_VERSION"
	fi
	DOCKER_BUILDKIT=1 "$RUNTIME" build "$@" -t "$TAG" "$REPO_ROOT"

	CID=$("$RUNTIME" create "$TAG")
	TMP=$(mktemp -d)
	"$RUNTIME" cp "${CID}":/opt/app/. "$TMP/"
	# Move only the freshly built .eap package(s) to the repo root.
	for eap in "$TMP"/*.eap; do
		[ -e "$eap" ] || continue
		mv "$eap" "$REPO_ROOT/"
	done
	rm -rf "$TMP"
	"$RUNTIME" rm -f "$CID" >/dev/null 2>&1 || true
	"$RUNTIME" rmi -f "$TAG" >/dev/null 2>&1 || true
}

# Build architectures sequentially: both share the same TAG namespace and
# copy through /opt/app, so serial builds keep the output unambiguous.
for ARCH in aarch64 armv7hf; do
	build_arch "$ARCH"
done

echo '==> Done!'
ls -lh "$REPO_ROOT"/*.eap
