The plugins in this directory are meant to
be used together; they will not be able to interact with other plugins
like `browser` and `filetransfer` in the top level directory.

NOTE: This is unpolished "research"-quality code. You should expect
rough edges.

# dependencies

The following are required in addition to those Shadow requires:

* `boost` (`sudo apt-get install libboost-dev-all` :))
* `spdylay` (https://github.com/tatsuhiro-t/spdylay)

# boost logging --- NO NEED: we don't use boost logging because shadow can't quite handle it

we use boost logging library because other libraries like google's
glog and easyloggingpp don't quite work under shadow (see
https://github.com/shadow/shadow/issues/315). however, due to shadow's
incomplete threading support, we want to use the single-threaded
version of the boost logging library. typically ubuntu provides
multi-threaded version, so we have to build our own:

```bash
# download boost 1.59 source, extract, then...:

# set up to install into shadow's install dir

./bootstrap.sh --prefix=$HOME/.shadow

# build only the logging library. the "myboostbuild" will be part of
# the .so file, so we will use -lboost_log-myboostbuild when building
# our plugins

./b2 --build-dir=builddir --layout=tagged --with-log \
     --buildid=myboostbuild \
     threading=single link=shared runtime-link=shared variant=release \
     define=BOOST_ALL_DYN_LINK define=BOOST_LOG_NO_THREADS \
     define=BOOST_LOG_WITHOUT_WCHAR_T define=BOOST_LOG_WITHOUT_SYSLOG \
     define=BOOST_LOG_NO_SHORTHAND_NAMES
```

# quick setup

from the top level `shadow-plugin-extras` dir:
```bash
mkdir build
cd build
CC=`which clang` CXX=`which clang++` cmake .. -DCMAKE_INSTALL_PREFIX=`readlink -f ~`/.shadow
```

**DOUBLE-CHECK (copied from old `web`)** If you installed `spdylay` in a custom location, specify `-DCMAKE_EXTRA_INCLUDES=/path/to/include -DCMAKE_EXTRA_LIBRARIES=/path/to/lib` when running `cmake`.


Then, build and install the plugins, by default to `$HOME/.shadow/plugins`:

```bash
make -jN
make install
```

Replace `N` with the number of cores you want to use for a parallel build.
