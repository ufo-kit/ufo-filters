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

    .. gobj:prop:: number:int

        Number of files to read.

    .. gobj:prop:: start:int

        First index from where files are read.

    .. gobj:prop:: step:int

        Number of files to skip.

    .. gobj:prop:: y:int

        Vertical coordinate from where to start reading.

    .. gobj:prop:: height:int

        Height of the region that is read from the image.

    .. gobj:prop:: convert:boolean

        Convert input data to float elements, enabled by default.

    .. gobj:prop:: type:string

        Overrides the type detection that is based on the file extension. For
        example, to load `.foo` files as raw files, set the ``type`` property to
        `raw`.

    .. gobj:prop:: raw-width:int

        Specifies the width of raw files.

    .. gobj:prop:: raw-height:int

        Specifies the height of raw files.

    .. gobj:prop:: raw-bitdepth:int

        Specifies the bit depth of raw files.


Memory reader
=============

.. gobj:class:: memory-in

    Reads data from a pre-allocated memory region. Unlike input and output tasks
    this can be used to interface with other code more directly, e.g. to read
    from a NumPy buffer::

        from gi.repository import Ufo
        import numpy as np


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

    .. gobj:prop:: width:int

        Specifies the width of input.

    .. gobj:prop:: height:int

        Specifies the height of input.

    .. gobj:prop:: number:int

        Specifies the number of items to read.


UcaCamera reader
================

.. gobj:class:: camera

    The camera task uses `libuca`_ to read frames from a connected camera and
    provides them as a stream. When :gobj:prop:`name` is provided, the
    corresponding plugin is instantiated by the camera task itself. However, an
    already configured UcaCamera object can also be passed via
    :gobj:prop:`camera`.

    The amount of processed frames can be controlled with either
    :gobj:prop:`count` or :gobj:prop:`time`.

    .. gobj:prop:: name:string

        Name of the camera that is used.

    .. gobj:prop:: number:int

        Number of frames that are recorded.

    .. gobj:prop:: time:float

        Duration over which frames are recorded.

    .. gobj:prop:: readout:boolean

        If *TRUE*, start read out instead of recording and grabbing live.

    .. _libuca: https://github.com/ufo-kit/libuca

    .. note:: This requires third-party library *libuca*.


stdin reader
============

.. gobj:class:: stdin

    Reads data from stdin to produce a valid data stream. :gobj:prop:`width`,
    :gobj:prop:`height` and :gobj:prop:`bitdepth` must be set correctly to
    ensure correctly sized data items.

    .. gobj:prop:: width:int

        Specifies the width of input.

    .. gobj:prop:: height:int

        Specifies the height of input.

    .. gobj:prop:: bitdepth:int

        Specifies the bit depth of input.


Metaball simulation
===================

.. gobj:class:: metaballs

    Generate animated meta balls. In each time step the meta balls move by a
    random velocity.

    .. gobj:prop:: width:int

        Width of output data stream.

    .. gobj:prop:: height:int

        Height of output data stream.

    .. gobj:prop:: number-balls:int

        Number of meta balls.

    .. gobj:prop:: number:int

        Length of data stream.

    .. gobj:prop:: frames-per-second:int

        Simulate behaviour by restricting the number of output images per
        second.


Data generation
===============

.. gobj:class:: dummy-data

    Only asks for image data sized :gobj:prop:`width` times :gobj:prop:`height`
    times :gobj:prop:`depth` and forwards :gobj:prop:`number` of them to the
    next filter. The data is never touched if :gobj:prop:`init` is not set, thus
    it might be suitable for performance measurements.

    .. gobj:prop:: width:int

        Width of image data stream.

    .. gobj:prop:: height:int

        Height of image data stream.

    .. gobj:prop:: depth:int

        Depth of image data stream.

    .. gobj:prop:: number:int

        Number of images to produce.

    .. gobj:prop:: init:float

        Value to initialize the output buffer.
