#!/usr/bin/env bash
# Build and push a kernel ORAS bundle locally.
# Usage: KERNEL_MM=6.18 ./tools/build-oras-local.sh

set -euo pipefail

KERNEL_MM="${KERNEL_MM:-6.18}"
REGISTRY="${REGISTRY:-ghcr.io}"
IMAGE_NAME="${IMAGE_NAME:-leftymods/CoreOS}"
BUNDLE_DIR="/tmp/linux-shallow-${KERNEL_MM}.git"
TAR_FILE="/tmp/linux-shallow-${KERNEL_MM}.git.tar"

echo "Building shallow bundle for Linux ${KERNEL_MM}..."

rm -rf "${BUNDLE_DIR}" /tmp/bare.git "${TAR_FILE}"
mkdir -p "${BUNDLE_DIR}"

# Try common stable branch names
if ! git clone --depth=1 --branch "v${KERNEL_MM}" \
    https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git \
    "${BUNDLE_DIR}" 2>/dev/null; then
  git clone --depth=1 --branch "linux-${KERNEL_MM}.y" \
    https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git \
    "${BUNDLE_DIR}"
fi

git init --bare /tmp/bare.git
cd "${BUNDLE_DIR}"
git push /tmp/bare.git --all
git push /tmp/bare.git --tags
tar -cf "${TAR_FILE}" -C /tmp bare.git

oras push "${REGISTRY}/${IMAGE_NAME}/kernel-git-shallow-${KERNEL_MM}:latest" \
  --artifact-type application/vnd.atrios.kernel.shallow \
  "${TAR_FILE}"

echo "Pushed ${REGISTRY}/${IMAGE_NAME}/kernel-git-shallow-${KERNEL_MM}:latest"
rm -rf "${BUNDLE_DIR}" /tmp/bare.git "${TAR_FILE}"
