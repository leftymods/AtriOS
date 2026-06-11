# Conventions for AI Agents

## Kernel commit pinning
- Kernel is pinned to a specific commit via `KERNELBRANCH="commit:<sha>"` in `config/sources/families/include/meson64_common.inc:44`
- When an updated kernel is needed, **I must**:
  1. Get the new HEAD SHA from `git ls-remote https://github.com/leftymods/linux-6.18.y.git main`
  2. Update the hash in `meson64_common.inc`
  3. Also update the fetch in `kernel-git-oras.sh` if the ref format changes (currently handles all prefixes via `${KERNELBRANCH##*:}`)
- Do NOT ask the user for the new hash — fetch it myself
