#!/usr/bin/env bash
# Original source https://github.com/Project-OSRM/osrm-backend/blob/master/scripts/format.sh

set +x
set -o errexit
set -o pipefail
set -o nounset

# Runs the Clang Formatter in parallel on the code base.
# Return codes:
#  - 1 there are files to be formatted
#  - 0 everything looks fine

# Get CPU count
OS=$(uname)
NPROC=1
if [[ $OS = "Linux" || $OS = "Darwin" ]] ; then
    NPROC=$(getconf _NPROCESSORS_ONLN)
fi

# Discover clang-format
if type clang-format-13 2> /dev/null ; then
    CLANG_FORMAT=clang-format-13
elif type clang-format 2> /dev/null ; then
    # Clang format found, but need to check version
    CLANG_FORMAT=clang-format
    V=$(clang-format --version)
    if [[ $V != *"version 13.0"* ]]; then
        echo "clang-format is not 13.0 (returned ${V})"
        exit 1
    fi
else
    echo "No appropriate clang-format found (expected clang-format-13.0.0, or clang-format)"
    exit 1
fi

find . -type d \( \
    -path ./\*build\* -o \
    -path ./release -o \
    -path ./cmake -o \
    -path ./deps \
\) -prune -false -type f -o \
    -name '*.h' -or \
    -name '*.hpp' -or \
    -name '*.m' -or \
    -name '*.mm' -or \
    -name '*.c' -or \
    -name '*.cpp' \
 | xargs -L100 -P ${NPROC} "${CLANG_FORMAT}" -i -style=file -fallback-style=none

find . -type d \( \
    -path ./\*build\* -o \
    -path ./release -o \
    -path ./deps \
\) -prune -false -type f -o \
    -name 'CMakeLists.txt' -or \
    -name '*.cmake' \
 | xargs -L10 -P ${NPROC} cmake-format -i
