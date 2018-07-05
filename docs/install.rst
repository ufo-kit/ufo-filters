============
Installation
============


Prior to building the filter suite you have to install the base library ufo-core
as well as all task-specific dependencies (e.g. *libtiff* for :gobj:class:`read`
and :gobj:class:`write`). Once ufo-core is installed you can check out the
source with::

  $ git clone https://github.com/ufo-kit/ufo-filters


Building with CMake
-------------------

Configure the build with::

  $ cd <source-path>
  $ cmake .

Installation paths can be customized by passing ``configure`` equivalents like
so::

  $ cmake . -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=/usr/lib64

Now build and install the filters with::

  $ make && make install

Depending on the installation location, the second step requires administration
rights.


Building with meson
-------------------

Configure the build with ``meson`` by changing into the root source directory
and type ::

  $ meson build

You can change the location of GNU installation directories during this step or
later with the ``meson configure`` tool ::

  $ meson build --prefix=/usr
  $ cd build && meson configure -Dprefix=/usr/local

Build, test and install everything with ::

  $ cd build
  $ ninja && ninja test && ninja install
