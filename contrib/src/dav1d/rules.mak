# libdav1d

DAV1D_HASH := 9075f0ee5799fabea9cc0460a261470cf1fafaf9
DAV1D_VERSION := git-$(DAV1D_HASH)
DAV1D_GITURL := https://code.videolan.org/videolan/dav1d.git

PKGS += dav1d
ifeq ($(call need_pkg,"libdav1d"),)
PKGS_FOUND += dav1d
endif

$(TARBALLS)/dav1d-$(DAV1D_VERSION).tar.xz:
	$(call download_git,$(DAV1D_GITURL),,$(DAV1D_HASH))

.sum-dav1d: dav1d-$(DAV1D_VERSION).tar.xz
	$(call check_githash,$(DAV1D_HASH))
	touch $@

dav1d: dav1d-$(DAV1D_VERSION).tar.xz .sum-dav1d
	$(UNPACK)
	$(MOVE)

.dav1d: dav1d crossfile.meson
	cd $< && rm -rf ./build
	cd $< && $(HOSTVARS_NOTOOLS) $(MESON) build
	cd $< && cd build && ninja install
	touch $@
