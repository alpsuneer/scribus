#!/usr/bin/env bash
# ============================================================================
#  Build + run the Scribus Kasm image locally, before pushing to a registry.
#
#  Usage:
#    ./kasm/test-local.sh              # build (no SAM models) and run
#    ./kasm/test-local.sh --with-models # bake SAM models into the image
#    ./kasm/test-local.sh --build-only  # just build, don't run
#
#  Run this from the repository ROOT (the build context must be the repo root).
# ============================================================================
set -euo pipefail

IMAGE="scribus-mdtp:kasm"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

BUILD_ARGS=()
RUN=1
for arg in "$@"; do
    case "$arg" in
        --with-models) BUILD_ARGS+=(--build-arg INCLUDE_SAM_MODELS=true) ;;
        --build-only)  RUN=0 ;;
        *) echo "Unknown option: $arg"; exit 1 ;;
    esac
done

echo "==> Building ${IMAGE} (context: ${REPO_ROOT})"
docker build -f kasm/Dockerfile.kasm "${BUILD_ARGS[@]}" -t "${IMAGE}" .

echo "==> Image built:"
docker images | grep -E "REPOSITORY|scribus-mdtp" || true

if [ "${RUN}" -eq 0 ]; then
    echo "==> --build-only: skipping run."
    exit 0
fi

mkdir -p "${REPO_ROOT}/kasm/test-workspace"

echo "==> Starting container"
echo "    Access at : https://localhost:6901"
echo "    Username  : kasm_user"
echo "    Password  : password"
echo "    (Ctrl+C to stop)"

docker run -it --rm \
    --shm-size=512m \
    -p 6901:6901 \
    -e VNC_PW=password \
    -v "${REPO_ROOT}/kasm/test-workspace:/home/kasm-user/Documents" \
    "${IMAGE}"
