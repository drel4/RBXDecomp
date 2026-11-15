#!/bin/sh
set -eu
cd "$(dirname "$0")"
docker run --rm --user "$(id -u):$(id -g)" -v "$PWD:/src" -w /src \
    "${PSPDEV_IMAGE:-pspdev/pspdev:latest}" make -j4
