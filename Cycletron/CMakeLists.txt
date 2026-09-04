cmake_minimum_required(VERSION 3.16.0)

# esp_lcd is unused (no display on this board) and its rgb panel driver
# reliably crashes this toolchain's GCC (ICE: segfault during RTL pass 'ira'
# in rgb_panel_draw_bitmap) - excluded to avoid the compiler crash.
set(EXCLUDE_COMPONENTS "esp_lcd")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(Cycletron)
