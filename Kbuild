######## Documentation

# In newer versions of the kernel, kbuild will first look for a file named
# "Kbuild," and only if that is not found, will it then look for a makefile.

# The kbuild system will build <module_name>.o from <module_name>.c,
# and, after linking, will result in the kernel module <module_name>.ko:
# obj-m := sbdd.o

# When the module is built from multiple sources, an additional line
# with '<module_name>-y := <src1>.o <src2>.o ...' is needed:
# Or you can do smth like this:
# sbdd-y := src1.o
# sbdd-y += src2.o
# ...

# kbuild supports building multiple modules with a single build file. For example,
# if you wanted to build two modules, foo.ko and bar.ko, the kbuild lines would be:
# obj-m := foo.o bar.o
# foo-y := <foo_srcs>
# bar-y := <bar_srcs>

# You can also add compile flags here, e.g.:
# ccflags-y := -I$(src)/include
# ccflags-y += -I$(src)/include
# About $(src): when kbuild executes, the current directory is always the root of
# the kernel tree (the argument to "-C") and therefore an absolute path is needed.
# $(src) provides the absolute path by pointing to the directory where the
# currently executing kbuild file is located.


######## Kbuild

ccflags-y := -Wall
# CFLAGS_sbdd.o := -DDEBUG

obj-m := sbdd.o
