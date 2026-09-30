# build_dir.sh — sourced by the run_* scripts to pick this platform's build.
#
# The Linux build lives in build/; binaries built on macOS go to build-mac/ so
# the two never overwrite each other (a Linux ELF in build/ will not run on a
# Mac, and vice versa).  Override with:  XDOS_BUILD=<dir> scripts/run_...sh
if [[ -z "${XDOS_BUILD:-}" ]]; then
    if [[ "$(uname -s)" == Darwin ]]; then XDOS_BUILD=build-mac; else XDOS_BUILD=build; fi
fi
