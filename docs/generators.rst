==========
Generators
==========

Generators produce data and have at least one output but no input.


Data readers
============

UcaCamera reader
----------------

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


TIFF/EDF reader
---------------

.. gobj:class:: read

    The reader loads single files from disk and provides them as a stream The
    nominal resolution can be decreased by specifying the :gobj:prop:`y`
    coordinate and a :gobj:prop:`height`. Due to reduced I/O, this can
    dramatically improve performance.

    .. gobj:prop:: path:string

        Glob-style pattern that describes the file path.

    .. gobj:prop:: start:int

        First index from where files are read.

    .. gobj:prop:: end:int

        Last index of file that is *not* read.

    .. gobj:prop:: step:int

        Number of files to skip.

    .. gobj:prop:: blocking:boolean

        Block until all files are read.

    .. gobj:prop:: normalize:boolean

        Whether 8-bit or 16-bit values are normalized to [0.0, 1.0].

    .. gobj:prop:: y:int

        Vertical coordinate from where to start reading.

    .. gobj:prop:: height:int

        Height of the region that is read from the image.

    .. gobj:prop:: enable-conversion:boolean

        Automatic conversion of input data to float.


Auxiliary generators
====================

Metaballs
---------

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


Empty data
----------

.. gobj:class:: dummy-data

    Only asks for image data sized :gobj:prop:`width` times :gobj:prop:`height`
    times :gobj:prop:`depth` and forwards :gobj:prop:`number` of them to the
    next filter. The data is never touched, thus it is suitable for performance
    measurements.

    .. gobj:prop:: width:int

        Width of image data stream.

    .. gobj:prop:: height:int

        Height of image data stream.

    .. gobj:prop:: depth:int

        Depth of image data stream.

    .. gobj:prop:: number:int

        Number of images to produce.
