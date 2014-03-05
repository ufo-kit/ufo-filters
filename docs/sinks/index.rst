=====
Sinks
=====

Sinks are endpoints and have at least one input but no output.


Data writers
============

TIFF writer
-----------

.. gobj:class:: writer

    Writes input data as TIFF files to the file system.

    .. gobj:prop:: filename:string

        Format string specifying the location and filename pattern of the
        written data. It must contain at most *one* integer format specifier
        that denotes the current index of a series. For example,
        ``"data-%03i.tif"`` produces ``data-001.tif``, ``data-002.tif`` and so
        on. If no specifier is given, a generic one is appended.

    .. gobj:prop:: single-file:boolean

        If *TRUE*, the writer writes a single multi-TIFF file instead of a sequence
        of TIFF files.

    .. note:: Requires *libtiff*.


Auxiliary sink
==============

Null
----

.. gobj:class:: null

    Eats input and discards it.

    .. gobj:prop:: force-download:boolean

        If *TRUE* force final data transfer from device to host if necessary.
