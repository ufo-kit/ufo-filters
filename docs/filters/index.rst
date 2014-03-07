=======
Filters
=======

Filters transform data and have at least one input and one output.

Point-based filters
===================

Averaging
---------

.. gobj:class:: averager

    Read in full data stream and generate an averaged output.

    .. gobj:prop:: num-generate:int

        Number of averaged images to output. By default one image is generated.


Flat-field correction
---------------------

.. gobj:class:: flat-field-correction

    Computes the flat field correction using three data streams:

    1. Projection data on input 0
    2. Dark field data on input 1
    3. Flat field data on input 2


    .. gobj:prop:: absorption-correction:boolean

        If *TRUE*, compute the negative natural logarithm of the
        flat-corrected data.

    .. gobj:prop:: fix-nan-and-inf:boolean

        If *TRUE*, replace all resulting NANs and INFs with zeros.


Generic OpenCL
--------------

.. gobj:class:: opencl

    Load an arbitrary :gobj:prop:`kernel` from :gobj:prop:`filename` or
    :gobj:prop:`source` and execute it on each input. The kernel must accept as
    many global float array parameters as connected to the filter and one
    additional as an output.

    .. gobj:prop:: filename:string

        Filename with kernel sources to load.

    .. gobj:prop:: source:string

        String with OpenCL kernel code.

    .. gobj:prop:: kernel:string

        Name of the kernel that this filter is associated with.

    .. gobj:prop:: num-dims:int

        Number of dimensions the kernel works on. Must be in [1, 3].


Complex filters
===============

Sinogram transposition
----------------------

.. gobj:class:: sino-generator

    Read a stream of two-dimensional projections and output a stream of
    transposed sinograms. :gobj:prop:`num-projections` *must* be set to the
    number of incoming projections to allocate enough memory.

    .. gobj:prop:: num-projections:int

        Number of projections.

    .. Warning::

        This is a memory intensive task and can easily exhaust your
        system memory. Make sure you have enough memory, otherwise the process
        will be killed.


Tomographic backprojection
--------------------------

.. gobj:class:: backproject

    Computes the backprojection for a single sinogram.

    .. gobj:prop:: axis-pos:float

        Position of the rotation axis in horizontal pixel dimension of a
        sinogram or projection. If not given, the center of the sinogram is
        assumed.

    .. gobj:prop:: angle-step:float

        Angle step increment in radians. If not given, pi divided by height
        of input sinogram is assumed.

    .. gobj:prop:: angle-offset:float

        Constant angle offset in radians. This determines effectively the
        starting angle.

    .. gobj:prop:: mode:enum

        Reconstruction mode which can be either ``nearest`` or ``texture``.


Forward projection
------------------

.. gobj:class:: forwardproject

    Computes the forward projection of slices into sinograms.

    .. gobj:prop:: num-projections:int

        Number of final 1D projections, that means height of the sinogram.

    .. gobj:prop:: angle-step:float

        Angular step between two adjacent projections. If not changed, it is
        simply pi divided by :gobj:prop:`num-projections`.


Gaussian blur
-------------

.. gobj:class:: gaussian-blur

    Blur image with a gaussian kernel.

    .. gobj:prop:: size:int

        Size of the kernel.

    .. gobj:prop:: sigma:int

        Sigma of the kernel.


Region of interest
------------------

.. gobj:class:: region-of-interest

    Cut out a region of interest from two-dimensional input. If the ROI is
    (partially) outside the input, only accessible data will be copied.

    .. gobj:prop:: x:int

        Horizontal coordinate from where to start the ROI.

    .. gobj:prop:: y:int

        Vertical coordinate from where to start the ROI.

    .. gobj:prop:: width:int

        Width of the region of interest.

    .. gobj:prop:: height:int

        Height of the region of interest.


Downsampling
------------

.. gobj:class:: downsample

    Reduce the size of the input data by a fixed :gobj:prop:`factor`.

    .. gobj:prop:: factor:int

        Fixed factor by which the input size is to be reduced.


Fast Fourier transform
----------------------

.. gobj:class:: fft

    Compute the Fourier spectrum of input data. If :gobj:prop:`dimensions` is one
    but the input data is 2-dimensional, the 1-D FFT is computed for each row.

    .. gobj:prop:: auto-zeropadding:boolean

        Automatically zeropad input data to a size to the next power of 2.

    .. gobj:prop:: dimensions:int

        Number of dimensions in [1, 3].

    .. gobj:prop:: size-x:int

        Size of FFT transform in x-direction.

    .. gobj:prop:: size-y:int

        Size of FFT transform in y-direction.

    .. gobj:prop:: size-z:int

        Size of FFT transform in z-direction.


.. gobj:class:: ifft

    Compute the inverse Fourier of spectral input data. If
    :gobj:prop:`dimensions` is one but the input data is 2-dimensional, the 1-D
    FFT is computed for each row.

    .. gobj:prop:: auto-zeropadding:boolean

        Automatically zeropad input data to a size to the next power of 2.

    .. gobj:prop:: dimensions:int

        Number of dimensions in [1, 3].

    .. gobj:prop:: size-x:int

        Size of FFT transform in x-direction.

    .. gobj:prop:: size-y:int

        Size of FFT transform in y-direction.

    .. gobj:prop:: size-z:int

        Size of FFT transform in z-direction.



Auxiliary filters
=================

Buffering
---------

.. gobj:class:: buffer

    Buffers items internally until data stream has finished. After that all
    buffered elements are forwarded to the next task.

    .. gobj:prop:: num-prealloc:int

        Number of pre-allocated buffers.
