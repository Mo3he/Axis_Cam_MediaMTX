#!/usr/bin/env sh
set -eu

REPO_ROOT=$(cd -P "$(dirname "$0")" && pwd)

# Prefer docker only when its daemon is reachable, else podman.
if [ -z "${RUNTIME:-}" ]; then
	if command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1; then
		RUNTIME=docker
	elif command -v podman >/dev/null 2>&1; then
		RUNTIME=podman
	elif command -v docker >/dev/null 2>&1; then
		# daemon not running: let docker fail with a clear error
		RUNTIME=docker
	else
		echo 'Error: neither docker nor podman found in PATH' >&2
		exit 1
	fi
fi
echo "==> Using container runtime: ${RUNTIME}"

# Optional MediaMTX release override (no leading 'v'); unset uses the Dockerfile pin.
MEDIAMTX_VERSION="${MEDIAMTX_VERSION:-}"

echo '==> Cleaning old .eap files...'
rm -f "${REPO_ROOT}"/*.eap
rm -rf "${REPO_ROOT}/debug"

# The SDK image is the final stage, so BuildKit --output cannot extract just the
# .eap; copy it out of a temporary container instead.
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
	# Unstripped binary for symbolising crash dumps; never shipped.
	mkdir -p "$REPO_ROOT/debug"
	"$RUNTIME" cp "${CID}":/opt/debug/mediamtx.unstripped \
		"$REPO_ROOT/debug/mediamtx-${ARCH}.unstripped"
	for eap in "$TMP"/*.eap; do
		[ -e "$eap" ] || continue
		mv "$eap" "$REPO_ROOT/"
	done
	rm -rf "$TMP"
	"$RUNTIME" rm -f "$CID" >/dev/null 2>&1 || true
	"$RUNTIME" rmi -f "$TAG" >/dev/null 2>&1 || true
}

for ARCH in aarch64 armv7hf; do
	build_arch "$ARCH"
done

echo '==> Done!'
ls -lh "$REPO_ROOT"/*.eap
