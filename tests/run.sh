#!/bin/sh
# Build the test image and run tests/e2e.sh inside a privileged container
# (privileged only so the *outer* container allows user namespaces; the
# runtime itself runs as the unprivileged user "dev").
set -e
cd "$(dirname "$0")/.."
docker build -q -t light-container-test tests >/dev/null
exec docker run --rm --privileged -v "$PWD":/src:ro light-container-test /src/tests/e2e.sh "$@"
