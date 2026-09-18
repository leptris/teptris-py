#!/usr/bin/env bash
# Vendor the pinned libteptris tag into vendor/ and build the sdist.
# The sdist then compiles the engine at pip-install time (setup.py's
# vendored mode) on any platform with a C compiler.
set -euo pipefail
cd "$(dirname "$0")/.."

C_TAG="${C_TAG:-$(grep -m1 'ref: v' .github/workflows/cibuildwheel.yml | awk '{print $2}')}"
echo "sdist rides ${C_TAG}"
rm -rf vendor .sdist-tmp
mkdir -p vendor .sdist-tmp
trap 'rm -rf .sdist-tmp vendor' EXIT
curl -sL "https://api.github.com/repos/leptris/teptris/tarball/${C_TAG}" \
  | tar xz -C .sdist-tmp --strip-components=1
mkdir -p vendor/libteptris
cp -R .sdist-tmp/src vendor/libteptris/src
cp .sdist-tmp/LICENSE.md vendor/libteptris/LICENSE.md
python3 -m build --sdist
