#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#
#
# ident	"%Z%%M%	%I%	%E% SMI"
#
# Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.

LIBRARY=	ldprof.a
VERS=		.1
COMOBJS=	profile.o
BLTOBJ=		msg.o
OBJECTS=	$(COMOBJS) $(BLTOBJ)

include		../../../../lib/Makefile.lib
include		../../Makefile.com

ZIGNORE=

ROOTLIBDIR=	$(ROOT)/usr/lib/link_audit

SGSPROTO=	../../proto/$(MACH)

MAPFILE=	mapfile-vers

# Building SUNWonld results in a call to the `package' target.  Requirements
# needed to run this application on older releases are established:
#   dlopen/dlclose requires libdl.so.1 prior to 5.10
# 
DLLIB =		$(VAR_DL_LIB)
package	:=	DLLIB = $(VAR_PKG_DL_LIB)

DYNFLAGS +=	-M$(MAPFILE) $(USE_PROTO)
CPPFLAGS=	-I. -I../common -I../../include \
		-I../../rtld/common \
		-I../../include/$(MACH) \
		-I$(SRCBASE)/lib/libc/inc \
		-I$(SRCBASE)/uts/common/krtld \
		-I$(SRC)/common/sgsrtcid \
		-I$(SRCBASE)/uts/$(ARCH)/sys \
		$(CPPFLAGS.master)
CFLAGS +=	$(C_PICFLAGS)
LDLIBS +=	-lmapmalloc -lc $(DLLIB)

LINTFLAGS +=	-u -erroff=E_NAME_DECL_NOT_USED_DEF2
LINTFLAGS64 +=	-u -erroff=E_NAME_DECL_NOT_USED_DEF2

BLTDEFS=	msg.h
BLTDATA=	msg.c
BLTMESG=	$(SGSMSGDIR)/ldprof

BLTFILES=	$(BLTDEFS) $(BLTDATA) $(BLTMESG)

SGSMSGCOM=	../common/ldprof.msg
SGSMSGTARG=	$(SGSMSGCOM)
SGSMSGALL=	$(SGSMSGCOM)
SGSMSGFLAGS +=	-h $(BLTDEFS) -d $(BLTDATA) -m $(BLTMESG) -n ldprof_msg

SRCS=		$(COMOBJS:%.o=../common/%.c) $(BLTDATA)
LINTSRCS=	$(SRCS) ../common/lintsup.c

CLEANFILES +=	$(LINTOUTS) $(BLTFILES)
CLOBBERFILES +=	$(DYNLIB) $(LINTLIB)

ROOTDYNLIB=	$(DYNLIB:%=$(ROOTLIBDIR)/%)
