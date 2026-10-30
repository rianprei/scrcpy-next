#!/bin/bash
set -ex
cd "$(dirname ${BASH_SOURCE[0]})"
. build_common
cd .. # root project dir

GRADLE="${GRADLE:-./gradlew}"
SERVER_BUILD_DIR="$WORK_DIR/build-server"

rm -rf "$SERVER_BUILD_DIR"
"$GRADLE" -p server assembleRelease
mkdir -p "$SERVER_BUILD_DIR/server"
cp server/build/outputs/apk/release/server-release-unsigned.apk \
    "$SERVER_BUILD_DIR/server/scrcpy-server"

# IME sidecar (--ime): signed with the release key if configured (see
# ime/README.md), with the debug key otherwise
if [[ -n "${SCRCPY_IME_STORE_FILE:-}" ]]
then
    GRADLE="$GRADLE" ime/scripts/build.sh release
else
    GRADLE="$GRADLE" ime/scripts/build.sh debug
fi
cp ime/dist/scrcpy-ime.apk "$SERVER_BUILD_DIR/server/scrcpy-ime.apk"
