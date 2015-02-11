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

Transposition
-------------

.. gobj:class:: transpose

    Transpose images from (x, y) to (y, x).


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


Phase retrieval
---------------

.. gobj:class:: phase-retrieval

    Computes correction of phase-shifted data.

    .. gobj:prop:: method:string

        Retrieval method which is one of ``tie``, ``ctf``, ``ctfhalfsin``,
        ``qp``, ``qphalfsine`` or ``qp2``.

    .. gobj:prop:: width:int

        Filter width (optional).

    .. gobj:prop:: height:int

        Filter height (optional).

    .. gobj:prop:: energy:float

        Energy in keV.

    .. gobj:prop:: distance:float

        Distance in meter.

    .. gobj:prop:: pixel-size:float

        Pixel size in meter.

    .. gobj:prop:: regularization-rate:float

        Regularization parameter is log10 of the constant to be added to the
        denominator to regularize the singularity at zero frequency: 1/sin(x) ->
        1/(sin(x)+10^-RegPar).
        
        Typical values [2, 3].

    .. gobj:prop:: thresholding-rate:float

        Parameter for Quasiparticle phase retrieval which defines the width of
        the rings to be cropped around the zero crossing of the CTF denominator
        in Fourier space.
        
        Typical values in [0.01, 0.1], ``qp`` retrieval is rather independent of
        cropping width.


Gaussian blur
-------------

.. gobj:class:: gaussian-blur

    Blur image with a gaussian kernel.

    .. gobj:prop:: size:int

        Size of the kernel.

    .. gobj:prop:: sigma:int

        Sigma of the kernel.


Padding
-------

.. gobj:class:: pad

    Pad an image to some extent with specific behavior for pixels falling
    outside the original image.

    .. gobj:prop:: x:int

        Horizontal coordinate in the output image which will contain the first
        input column.

    .. gobj:prop:: y:int

        Vertical coordinate in the output image which will contain the first
        input row.

    .. gobj:prop:: width:int

        Width of the padded image.

    .. gobj:prop:: height:int

        Height of the padded image.

    .. gobj:prop:: addressing-mode:string

        Addressing mode specifies the behavior for pixels falling outside the
        original image. See OpenCL sampler_t documentation for more information.


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


Slice mapping
-------------

.. gobj:class:: map-slice

    Lays out input images on a quadratic grid. If the :gobj:prop:`number` of
    input elements is not the square of some integer value, the next higher
    number is chosen and the remaining data is blackened.

    .. gobj:prop:: factor:int

        Number of expected input elements. If more elements are sent to the
        mapper, warnings are issued.


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


Slicing
-------

.. gobj:class:: slice

    Slices a three-dimensional input buffer to two-dimensional slices.


Stacking
--------

.. gobj:class:: stack

    Symmetrical to the slice filter, the stack filter stacks two-dimensional
    input.

    .. gobj:prop:: num-items:int

        Number of items, i.e. the length of the third dimension.

