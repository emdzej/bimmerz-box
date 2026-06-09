# Bimmerz Box firmware — top-level project CMake.
#
# Standard ESP-IDF project entry point. Components live under
# firmware/components/ and are auto-discovered. The main component lives
# under firmware/main/.

cmake_minimum_required(VERSION 3.16)

# Components living next to main/ rather than under components/ need to
# be added here. admin_ui is a top-level firmware concern that owns its
# own embedded HTML/JS assets — keeping it out of components/ makes the
# separation clearer.
set(EXTRA_COMPONENT_DIRS admin_ui)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(bimmerz_box C)
