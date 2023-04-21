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


Memory reader
=============

.. gobj:class:: memory-in

    Reads data from a pre-allocated memory region. Unlike input and output tasks
    this can be used to interface with other code more directly, e.g. to read
    from a NumPy buffer or an OpenCL buffer::

        import gi
        import numpy as np
        import tifffile
        gi.require_version("Ufo", "0.0")
        from gi.repository import Ufo


        USE_OPENCL = True


        pm = Ufo.PluginManager()
        sched = Ufo.FixedScheduler()
        graph = Ufo.TaskGraph()

        mem_in = pm.get_task("memory-in")
        writer = pm.get_task("write")

        n = 4096
        mem_in.props.width = n
        mem_in.props.height = n
        mem_in.props.bitdepth = 32
        mem_in.props.number = 1

        writer.props.filename = "out.tif"
        writer.props.tiff_bigtiff = False

        if USE_OPENCL:
            import pyopencl as cl
            import pyopencl.array as pa

            res = Ufo.Resources()
            # Use UFO's OpenCL context
            ctx = cl.Context.from_int_ptr(res.get_context())
            queue = cl.CommandQueue(ctx, ctx.devices[0])
            # And make sure UFO does not create a new one
            sched.set_resources(res)

            ref_buf = pa.arange(queue, n ** 2, dtype=np.float32).reshape(n, n)
            mem_in.props.memory_location = "buffer"
            mem_in.props.pointer = ref_buf.data.int_ptr
            ref = ref_buf.get()  # For checking later
        else:
            ref = np.arange(n ** 2, dtype=np.float32).reshape(n, n)
            mem_in.props.memory_location = "host"
            mem_in.props.pointer = ref.__array_interface__["data"][0]

        graph.connect_nodes(mem_in, writer)
        sched.run(graph)

        out = tifffile.imread("out.tif")
        assert np.sum(out - ref) == 0.0

    .. gobj:prop:: pointer:ulong

        Pointer to pre-allocated memory.

    .. gobj:prop:: width:uint

        Specifies the width of input.

    .. gobj:prop:: height:uint

        Specifies the height of input.

    .. gobj:prop:: number:uint

        Specifies the number of items to read.

    .. gobj:prop:: complex-layout:boolean

        Treat input as interleaved complex64 data type (x[0] = Re(z[0]), x[1] = Im(z[0]), ...)

    .. gobj:prop:: memory-location:enum

        Location of the input memory [``host`` (RAM), ``buffer`` (OpenCL buffer)]


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
