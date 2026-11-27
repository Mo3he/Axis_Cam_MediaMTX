#!/usr/bin/env sh
# Unit tests for the MP4/box parsing and request helpers in config.c, run with
# Address/UB sanitizers against a stub FastCGI header, and a check that every
# commented example in the bundled defaults still parses when uncommented.
# Run by CI (ci.yml, build.yml) and by acap-build.sh before every release.
set -eu
cd "$(dirname "$0")/.."
cc=${CC:-cc}
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
"$cc" -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
	-Itests/fcgi_stub tests/test_mp4.c -o "$out/test_mp4"
"$out/test_mp4"
ruby tests/test_defaults.rb
