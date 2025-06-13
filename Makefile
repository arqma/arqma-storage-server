dotgit=$(shell ls -d .git/config)
ifneq ($(dotgit), .git/config)
  USE_SINGLE_BUILDDIR=1
endif

subbuilddir:=$(shell echo  `uname | sed -e 's|[:/\\ \(\)]|_|g'`/`git branch | grep '\* ' | cut -f2- -d' '| sed -e 's|[:/\\ \(\)]|_|g'`)
ifeq ($(USE_SINGLE_BUILDDIR),)
  builddir := build/"$(subbuilddir)"
  topdir   := ../../../..
  deldirs  := $(builddir)
else
  builddir := build
  topdir   := ../..
  deldirs  := $(builddir)/debug $(builddir)/release
endif

all: release-all

debug: debug-all

debug-all:
	mkdir -p $(builddir)/debug
	cd $(builddir)/debug && cmake -D BUILD_STATIC_DEPS=ON -D CMAKE_POLICY_VERSION_MINIMUM=3.18 -D CMAKE_BUILD_TYPE=Debug -D DISABLE_SNODE_SIGNATURE=OFF $(topdir) && $(MAKE)

debug-sig-off:
	mkdir -p $(builddir)/debug
	cd $(builddir)/debug && cmake -D BUILD_STATIC_DEPS=ON -D CMAKE_POLICY_VERSION_MINIMUM=3.18 -D CMAKE_BUILD_TYPE=Debug -D DISABLE_SNODE_SIGNATURE=ON $(topdir) && $(MAKE)

release-all:
	mkdir -p $(builddir)/release
	cd $(builddir)/release && cmake -D CMAKE_POLICY_VERSION_MINIMUM=3.18 -D BUILD_STATIC_DEPS=ON -D CMAKE_BUILD_TYPE=Release -D DISABLE_SNODE_SIGNATURE=OFF $(topdir) && $(MAKE)

release-sig-off:
	mkdir -p $(builddir)/release
	cd $(builddir)/release && cmake -D CMAKE_POLICY_VERSION_MINIMUM=3.18 -D BUILD_STATIC_DEPS=ON -D CMAKE_BUILD_TYPE=Release -D DISABLE_SNODE_SIGNATURE=ON $(topdir) && $(MAKE)

clean:
	rm -rf $(deldirs)

clean-all:
	rm -rf ./build

format:
	clang-format -style=file -i \
		crypto/**/*.{cpp,hpp} \
		storage/**/*.{cpp,hpp} \
		utils/**/*.{cpp,hpp} \
		httpserver/*.{cpp,h} \
		common/**/*.{cpp,h}

tags:
	ctags -R --sort=1 --c++-kinds=+p --fields=+iaS --extra=+q --language-force=C++ common crypto httpserver storage utils vendors

.PHONY: all release-all debug debug-all clean format tags
