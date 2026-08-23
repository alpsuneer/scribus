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

# ---------------------------------------------------------------------------
# BuildKit's build containers do not always inherit working DNS — on a host
# running an IPsec/VPN daemon, `docker run` resolves fine while `docker build`
# fails every apt step with "Temporary failure resolving 'deb.debian.org'".
# Probe once and fall back to the host network rather than letting a 4-minute
# apt stage time out and report it as a missing-package error.
# ---------------------------------------------------------------------------
NET_ARGS=()
probe_dir="$(mktemp -d)"
trap 'rm -rf "${probe_dir}"' EXIT
printf 'FROM debian:trixie-slim\nRUN getent hosts deb.debian.org\n' > "${probe_dir}/Dockerfile"
if docker build -q -f "${probe_dir}/Dockerfile" "${probe_dir}" >/dev/null 2>&1; then
    echo "==> Build-network DNS OK"
else
    echo "==> Build-network DNS is broken; falling back to --network=host"
    NET_ARGS+=(--network=host)
fi

echo "==> Building ${IMAGE} (context: ${REPO_ROOT})"
docker build "${NET_ARGS[@]}" -f kasm/Dockerfile.kasm "${BUILD_ARGS[@]}" -t "${IMAGE}" .

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
