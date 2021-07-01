#
# module.mk
#
# Copyright (C) 2010 Alfred E. Heggestad
#

MOD		:= libdssipapp
$(MOD)_SRCS	+= dssipapp.c
$(MOD)_LFLAGS	+= $(shell pkg-config --libs libbaresip)
$(MOD)_CFLAGS	+= $(shell pkg-config --libs libbaresip)

include mk/mod.mk
