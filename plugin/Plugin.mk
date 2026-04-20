# plugin/Plugin.mk — shared build rules for firedancer plugins.
#
# A plugin's Local.mk sets these variables before including:
#
#   PLUGIN_NAME       — short name (e.g., myplugin)
#   PLUGIN_TOPO_SRCS  — topology source basenames (without .c)
#   PLUGIN_TILE_SRCS  — tile implementation basenames (without .c)
#
# Example Local.mk:
#
#   PLUGIN_NAME      := myplugin
#   PLUGIN_TOPO_SRCS := myplugin_topo
#   PLUGIN_TILE_SRCS := myplugin_tile
#   include plugin/Plugin.mk

ifndef PLUGIN_NAME
$(error PLUGIN_NAME is not set.  Set it before including Plugin.mk)
endif

# Library name for this plugin's objects
_PLUGIN_LIB := fd_plugin_$(PLUGIN_NAME)

# Compile topology sources into the plugin library
ifdef PLUGIN_TOPO_SRCS
$(call add-objs,$(PLUGIN_TOPO_SRCS),$(_PLUGIN_LIB))
endif

# Compile tile sources into the plugin library
ifdef PLUGIN_TILE_SRCS
$(call add-objs,$(PLUGIN_TILE_SRCS),$(_PLUGIN_LIB))
endif

# Create the static library
$(call make-lib,$(_PLUGIN_LIB))

# Accumulate plugin metadata for registry generation (task 6).
# Each plugin appends its name to a global list.
PLUGIN_NAMES += $(PLUGIN_NAME)

# Record the topology function name (default: fd_$(PLUGIN_NAME)_topo)
PLUGIN_TOPO_FN_$(PLUGIN_NAME) ?= fd_$(PLUGIN_NAME)_topo

# Record tile run struct names (default: fd_tile_<basename> for each tile src)
ifdef PLUGIN_TILE_SRCS
PLUGIN_TILE_RUNS_$(PLUGIN_NAME) := $(foreach src,$(PLUGIN_TILE_SRCS),fd_tile_$(src))
endif

# Record the library name so the linker can find it
PLUGIN_LIBS += $(_PLUGIN_LIB)

# Default clean target for this plugin (removes generated/).
# A plugin's Local.mk may override clean-plugin-NAME before or
# after including Plugin.mk to add custom cleanup.
.PHONY: clean-plugin-$(PLUGIN_NAME)
clean-plugin-$(PLUGIN_NAME)::
	$(RMDIR) $(MKPATH)generated

PLUGIN_CLEAN_TARGETS += clean-plugin-$(PLUGIN_NAME)

# Generate seccomp headers for any .seccomppolicy files in the
# plugin directory.  The generate_filters.py script writes the
# output next to the policy file (plugin/NAME/generated/).
_PLUGIN_POLICIES := $(wildcard $(MKPATH)*.seccomppolicy)
ifdef _PLUGIN_POLICIES
$(foreach pol,$(_PLUGIN_POLICIES),$(shell $(PYTHON) contrib/codegen/generate_filters.py $(pol)))
endif

# Clean up local variables to avoid leaking into the next plugin
undefine _PLUGIN_LIB
undefine _PLUGIN_POLICIES