PKG_NAME=emb6
PKG_URL=git://github.com/hso-esk/emb6.git
PKG_VERSION=14e4a3cfff01644e078870e14e16a1fe60dcc895
PKG_DIR=$(CURDIR)/$(PKG_NAME)

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
	@echo "Cleaning up emb6 package..."
	@cd "$(PKG_DIR)" 2> /dev/null > /dev/null && \
		git clean -x -f && \
		git am --abort && \
		git reset --hard "$(PKG_VERSION)" && \
		$(MAKE) patch || true


distclean::
	rm -rf "$(PKG_DIR)"

Makefile.include:
	@true
