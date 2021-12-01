==========
Generators
==========

Generators produce data and have at least one output but no input.


File reader
===========

.. gobj:class:: read

    The reader loads single files from disk to produce a stream of
    two-dimensional data items. Supported file types depend on the compiled
    plugin. Raw (`.raw`) and EDF (`.edf`) files can always be read without
    additional support. Additionally, loading TIFF (`.tif` and `.tiff`) and HDF5
    (`.h5`) files might be supported.

    The nominal resolution can be decreased by specifying the :gobj:prop:`y`
    coordinate and a :gobj:prop:`height`. Due to reduced I/O, this can
    dramatically improve performance.

    .. gobj:prop:: path:string

        Glob-style pattern that describes the file path. For HDF5 files this
        must point to a file and a data set separated by a colon, e.g.
        ``/path/to/file.h5:/my/data/set``.

    .. gobj:prop:: number:uint

        Number of files to read.

    .. gobj:prop:: start:uint

        First index from where files are read.

    .. gobj:prop:: image-start:uint

        First image index from where images are read in multi-image files.

    .. gobj:prop:: step:uint

        Number of files to skip.

    .. gobj:prop:: image-step:uint

        Number of images to skip in a multi-image file.

    .. gobj:prop:: y:uint

        Vertical coordinate from where to start reading.

    .. gobj:prop:: height:uint

        Height of the region that is read from the image.

    .. gobj:prop:: y-step:uint

        Read every ``y-step`` row.

    .. gobj:prop:: convert:boolean

        Convert input data to float elements, enabled by default.

    .. gobj:prop:: raw-width:uint

        Specifies the width of raw files.

    .. gobj:prop:: raw-height:uint

        Specifies the height of raw files.

    .. gobj:prop:: raw-bitdepth:uint

        Specifies the bit depth of raw files.

    .. gobj:prop:: raw-pre-offset:ulong

        Offset that is skipped before reading the next frame from the current file.

    .. gobj:prop:: raw-post-offset:ulong

        Offset that is skipped after reading the last frame from the current file.

    .. gobj:prop:: type:enum

        Overrides the type detection that is based on the file extension. For
        example, to load `.foo` files as raw files, set the ``type`` property to
        `raw`.

    .. gobj:prop:: retries:uint

        Set the number of retries in case files do not exist yet and are being
        written. If you set this, you *must* also set ``number`` otherwise you
        would have to wait basically forever for the execution to finish. Note,
        that only files are considered which come after the last successful
        filename.

    .. gobj:prop:: retry-timeout:uint

        Seconds to wait before reading new files.


Memory reader
=============

.. gobj:class:: memory-in

    Reads data from a pre-allocated memory region. Unlike input and output tasks
    this can be used to interface with other code more directly, e.g. to read
    from a NumPy buffer::

        from gi.repository import Ufo
        import numpy as np
        import tifffile


        ref = np.random.random((512, 512)).astype(np.float32)

        pm = Ufo.PluginManager()
        g = Ufo.TaskGraph()
        sched = Ufo.Scheduler()
        read = pm.get_task('memory-in')
        write = pm.get_task('write')

        read.props.pointer = ref.__array_interface__['data'][0]
        read.props.width = ref.shape[1]
        read.props.height = ref.shape[0]
        read.props.number = 1

        write.props.filename = 'out.tif'

        g.connect_nodes(read, write)
        sched.run(g)

        out = tifffile.imread('out.tif')
        assert np.sum(out - ref) == 0.0

    .. gobj:prop:: pointer:ulong

        Pointer to pre-allocated memory.

    .. gobj:prop:: width:uint

        Specifies the width of input.

    .. gobj:prop:: height:uint

        Specifies the height of input.

    .. gobj:prop:: number:uint

        Specifies the number of items to read.


ZeroMQ subscriber
=================

.. gobj:class:: zmq-sub

    Generates a stream from a compatible ZeroMQ data stream, for example
    published by the :gobj:class:`zmq-pub` task.

    .. gobj:prop:: address:string

        Host address of the ZeroMQ publisher. Note, that as of now the publisher
        binds to a ``tcp`` endpoint, thus you have to use that as well. By
        default, the address is set to the local host address 127.0.0.1.


UcaCamera reader
================

.. gobj:class:: camera

    The camera task uses `libuca`_ to read frames from a connected camera and
    provides them as a stream.

    When :gobj:prop:`name` is provided, the corresponding plugin is instantiated
    by the camera task itself. However, an already configured UcaCamera object
    can also be passed via :gobj:prop:`camera`.

    .. gobj:prop:: name:string

        Name of the camera that is used.

    .. gobj:prop:: number:uint

        Number of frames that are recorded.

    .. gobj:prop:: properties:string

        Property string, i.e. ``roi-width=512 exposure-time=0.1``.

    .. _libuca: https://github.com/ufo-kit/libuca

    .. note:: This requires third-party library *libuca*.


stdin reader
============

.. gobj:class:: stdin

    Reads data from stdin to produce a valid data stream. :gobj:prop:`width`,
    :gobj:prop:`height` and :gobj:prop:`bitdepth` must be set correctly to
    ensure correctly sized data items.

    .. gobj:prop:: width:uint

        Specifies the width of input.

    .. gobj:prop:: height:uint

        Specifies the height of input.

    .. gobj:prop:: bitdepth:uint

        Specifies the bit depth of input.

    .. gobj:prop:: convert:boolean

        Convert input data types to float, enabled by default.


Metaball simulation
===================

.. gobj:class:: metaballs

    Generate animated meta balls. In each time step the meta balls move by a
    random velocity.

    .. gobj:prop:: width:uint

        Width of output data stream.

    .. gobj:prop:: height:uint

        Height of output data stream.

    .. gobj:prop:: number-balls:uint

        Number of meta balls.

    .. gobj:prop:: number:uint

        Length of data stream.


Data generation
===============

.. gobj:class:: dummy-data

    Only asks for image data sized :gobj:prop:`width` times :gobj:prop:`height`
    times :gobj:prop:`depth` and forwards :gobj:prop:`number` of them to the
    next filter. The data is never touched if :gobj:prop:`init` is not set, thus
    it might be suitable for performance measurements.

    .. gobj:prop:: width:uint

        Width of image data stream.

    .. gobj:prop:: height:uint

        Height of image data stream.

    .. gobj:prop:: depth:uint

        Depth of image data stream.

    .. gobj:prop:: number:uint

        Number of images to produce.

    .. gobj:prop:: init:float

        Value to initialize the output buffer.
