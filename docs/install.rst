============
Installation
============

Prior to building the filter suite you have to install the base library ufo-core
as well as all task-specific dependencies (e.g. *libtiff* for :gobj:class:`read`
and :gobj:class:`write`). Once installed you can check out the source with::

    $ git clone https://github.com/ufo-kit/ufo-filters

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
