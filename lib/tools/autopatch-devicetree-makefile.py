# ‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹
#  SPDX-License-Identifier: GPL-2.0
#  Copyright (c) 2025-2026 leftymods
#  This file is a part of the Armbian Build Framework https://github.com/armbian/build/
# ‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹

# So this thing takes
# - an absolute path to a kernel check-ed out git tree (GIT_WORK_DIR=/Volumes/LinuxDev/mainline-kernel-3rd-party-rebase)
# - the relative path to a Device Tree directory (DT_REL_DIR=arch/arm64/boot/dts/amlogic)
# and will for the DT_REL_DIR:
# - find all the .dts files
# - find the Makefile
# It will then regex-parse the Makefile for the CONFIG_ARCH_xxx variable, find the preamble and postamble, and insert the DT files in between.

import logging

import common.atrios_utils as atrios_utils
import common.dt_makefile_patcher as dt_makefile_patcher

# Prepare logging
atrios_utils.setup_logging()
log: logging.Logger = logging.getLogger("patching")

# Show the environment variables we've been called with
atrios_utils.show_incoming_environment()

GIT_WORK_DIR = atrios_utils.get_from_env("GIT_WORK_DIR", "/Volumes/LinuxDev/mainline-kernel-3rd-party-rebase")
DT_REL_DIR = atrios_utils.get_from_env("DT_REL_DIR", "arch/arm64/boot/dts/amlogic")

dt_makefile_patcher.auto_patch_dt_makefile(GIT_WORK_DIR, DT_REL_DIR)
