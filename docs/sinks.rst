=====
Sinks
=====

Sinks are endpoints and have at least one input but no output.


Data writers
============

File writer
-----------

.. gobj:class:: write

    Writes input data to the file system. Support for writing depends on compile
    support, however raw (`.raw`) files can always be written. TIFF (`.tif` and
    `.tiff`), HDF5 (`.h5`) and JPEG (`.jpg` and `.jpeg`) might be supported
    additionally.

    .. gobj:prop:: filename:string

        Format string specifying the location and filename pattern of the
        written data. It must contain at most *one* integer format specifier
        that denotes the current index of a series. For example,
        ``"data-%03i.tif"`` produces ``data-001.tif``, ``data-002.tif`` and so
        on. If no specifier is given, the data is written preferably to a single
        file (i.e. multi-tiff, HDF5 data set).

    .. gobj:prop:: append:boolean

        Append rather than overwrite if ``TRUE``.

    .. gobj:prop:: bits:int

        Number of bits to store the data if applicable to the file format.
        Possible values are 8 and 16 which are saved as integer types and 32 bit
        float.

    For JPEG files the following property applies:

    .. gobj:prop:: quality:int

        JPEG quality value between 0 and 100. Higher values correspond to higher
        quality and larger file sizes.


stdout writer
-------------

.. gobj:class:: stdout

    Writes input to stdout. To chop up the data stream you can use the UNIX tool split.

    .. gobj:prop:: bits

        Number of bits for final conversion.  Possible values are 8 and 16 which
        are saved as integer types and 32 bit float.


Auxiliary sink
==============

Null
----

.. gobj:class:: null

    Eats input and discards it.

    .. gobj:prop:: force-download:boolean

        If *TRUE* force final data transfer from device to host if necessary.
