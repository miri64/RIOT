PKG_NAME=microcoap
PKG_URL=https://github.com/1248/microcoap.git
PKG_VERSION=6fc9b1ab23284632483f13ccd24bd271023dad22
PKG_DIR=$(CURDIR)/$(PKG_NAME)

ifneq ($(RIOTBOARD),)
include $(RIOTBOARD)/$(BOARD)/Makefile.include
endif

ifneq ($(RIOTBASE),)
INCLUDES += -I$(RIOTBASE)/sys/include -I$(RIOTBASE)/sys/net/include \
			-I$(RIOTBASE)/sys/posix/include -I$(RIOTBASE)/sys/posix/pnet/include
endif

.PHONY: all clean patch reset

all: patch
	"$(MAKE)" -C $(PKG_DIR)

patch: $(PKG_DIR)/Makefile

$(PKG_DIR)/Makefile: $(PKG_DIR)/.git/config
	cd "$(PKG_DIR)" && git am --ignore-whitespace "$(CURDIR)"/*.patch

$(PKG_DIR)/.git/config:
	test -d "$(PKG_DIR)" || git clone "$(PKG_URL)" "$(PKG_DIR)"; \
		cd "$(PKG_DIR)" && git checkout -f "$(PKG_VERSION)"

clean::
	@echo "Cleaning up $(PKG_NAME) package..."
	@cd "$(PKG_DIR)" 2> /dev/null > /dev/null && \
		git clean -x -f && \
		git am --abort && \
		git reset --hard "$(PKG_VERSION)" && \
		$(MAKE) patch || true


distclean::
	rm -rf "$(PKG_DIR)"

Makefile.include:
	@true
