#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
exec python3 -m unittest \
    tools.customize.test_build_adapter \
    tools.customize.test_installer_recipe \
    tools.customize.test_base_build
