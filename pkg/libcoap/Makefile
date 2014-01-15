PKG_NAME=libcoap
PKG_URL=http://git.code.sf.net/p/libcoap/code
PKG_VERSION=89acaa6775ca3a4aedea510557e6b9c0c01fa5db

ifneq ($(RIOTBOARD),)
include $(RIOTBOARD)/Makefile.base
include $(RIOTBOARD)/$(BOARD)/Makefile.include
endif

ifneq ($(RIOTBASE),)
INCLUDES += -I$(RIOTBASE)/sys/include -I$(RIOTBASE)/sys/net/include \
			-I$(RIOTBASE)/sys/posix/include -I$(RIOTBASE)/sys/posix/pnet/include
endif

.PHONY: all clean patch reset

all: patch
	make -C $(CURDIR)/$(PKG_NAME)

patch: $(CURDIR)/$(PKG_NAME)/Makefile

$(CURDIR)/$(PKG_NAME)/Makefile: $(CURDIR)/$(PKG_NAME)
	$(foreach patch,$(shell ls [0-9][0-9][0-9][0-9]*.patch),cd "$<" && git am --ignore-whitespace "$(CURDIR)/$(patch)";)

$(CURDIR)/$(PKG_NAME)/:
	git clone $(PKG_URL) $@ && \
		cd $@ && git checkout $(PKG_VERSION)

clean::
	cd $(CURDIR)/$(PKG_NAME) && \
		git clean -x -f && \
		git reset --hard $(PKG_VERSION) || true

distclean::
	rm -rf $(CURDIR)/$(PKG_NAME)
