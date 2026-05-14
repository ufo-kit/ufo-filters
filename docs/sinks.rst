=====
Sinks
=====

Sinks are endpoints and have at least one input but no output.


File writer
===========

.. gobj:class:: write

    Writes input data to the file system. Support for writing depends on compile
    support, however raw (`.raw`) files can always be written. TIFF (`.tif` and
    `.tiff`), HDF5 (`.h5`), JPEG (`.jpg` and `.jpeg`) and JPEG 2000 (`.jp2`,
    `.j2k` and `.j2c`) might be supported additionally. By default,
    :gobj:prop:`bytes-per-file` is set to 128 GB, set it to 0 if you want to
    write single-page files.

    .. gobj:prop:: filename:string

        Format string specifying the location and filename pattern of the
        written data. It must contain at most *one* integer format specifier
        that denotes the current index of a series. For example,
        ``"data-%03i.tif"`` produces ``data-001.tif``, ``data-002.tif`` and so
        on. If no specifier is given, the data is written preferably to a single
        file (i.e. multi-tiff, HDF5 data set). If no filename is given the data
        is written as-is to stdout.

    .. gobj:prop:: counter-start:uint

        Sets the counter that replaces the format specifier. Initially, it is
        set to 0.

    .. gobj:prop:: counter-step:uint

        Determines the number of steps the counter replacing the format
        specifier is incremented. Initially, it is set to 1.

    .. gobj:prop:: bytes-per-file:ulong

        Bytes per file for multi-page files.

    .. gobj:prop:: append:boolean

        Append rather than overwrite if ``TRUE``.

    .. gobj:prop:: bits:uint

        Number of bits to store the data if applicable to the file format.
        Possible values are 8 and 16 which are saved as integer types and 32 bit
        float. By default, the minimum and maximum for scaling is determined
        automatically, however depending on the use case you should override
        this with the ``minimum`` and ``maximum`` properties. To avoid
        rescaling, set the ``rescale`` property to ``FALSE``.

    .. gobj:prop:: minimum:float

        This value will represent zero for discrete bit depths, i.e. 8 and 16
        bit.

    .. gobj:prop:: minimum:float

        This value will represent the largest possible value for discrete bit
        depths, i.e. 8 and 16 bit.

    .. gobj:prop:: rescale:boolean

        If ``TRUE`` and ``bits`` is set to a value less than 32, rescale values
        either by looking for minimum and maximum values or using the values
        provided by the user.

    For JPEG files the following property applies:

    .. gobj:prop:: jpeg-quality:uint

        JPEG quality value between 0 and 100. Higher values correspond to higher
        quality and larger file sizes.

    For TIFF files the following properties apply:

    .. gobj:prop:: tiff-bigtiff:boolean

        Whether to write in BigTiff format (required for files larger than 4
        GB).

    .. gobj:prop:: tiff-jpeg2000:boolean

        Compress TIFF pages with JPEG 2000 instead of writing uncompressed TIFF
        image data. Only 8 and 16 bit unsigned data are written directly; other
        input depths are converted to 16 bit before compression.

    .. gobj:prop:: tile-size:uint

        Square tile size for JPEG 2000-compressed TIFF output. The default value
        0 writes one JPEG 2000 codestream per TIFF page as a single strip. A
        value greater than 0 writes a tiled TIFF with one JPEG 2000 codestream
        per tile.

    For JPEG 2000 output and JPEG 2000-compressed TIFF output, OpenJPEG uses
    the number of available CPU cores automatically. The following property
    applies:

    .. gobj:prop:: level:uint

        JPEG 2000 quality level. The default value 0 writes lossless data.
        Values from 1 to 100 enable lossy compression with progressively higher
        quality.


Memory writer
=============

.. gobj:class:: memory-out

    Writes input to a given memory location. Unlike input and output tasks this
    can be used to interface with other code more directly, e.g. to write into a
    NumPy buffer::

        from gi.repository import Ufo
        import numpy as np
        import tifffile

        ref = tifffile.imread('data.tif')
        a = np.zeros_like(ref)

        pm = Ufo.PluginManager()
        g = Ufo.TaskGraph()
        sched = Ufo.Scheduler()
        read = pm.get_task('read')
        out = pm.get_task('memory-out')

        read.props.path = 'data.tif'
        out.props.pointer = a.__array_interface__['data'][0]
        out.props.max_size = ref.nbytes

        g.connect_nodes(read, out)
        sched.run(g)

        assert np.sum(a - ref) == 0.0

    .. gobj:prop:: pointer:ulong

        Pointer to pre-allocated memory.

    .. gobj:prop:: max-size:ulong

        Size of the pre-allocated memory area in bytes. Data is written up to
        that point only.


ZeroMQ publisher
================

.. gobj:class:: zmq-pub

    Publishes the stream as a ZeroMQ data stream to compatible ZeroMQ
    subscribers such as the :gobj:class:`zmq-sub` source.

    .. gobj:prop:: expected-subscribers:uint

        If set, the publisher will wait until the number of expected subscribers
        have connected.


Auxiliary sink
==============

Null
====

.. gobj:class:: null

    Eats input and discards it.

    .. gobj:prop:: download:boolean

        If *TRUE* force final data transfer from device to host if necessary.

    .. gobj:prop:: finish:boolean

        Call finish on the associated command queue.

    .. gobj:prop:: durations:boolean

        Print durations computed from timestamps on ``stderr``.
