=======
Filters
=======

Filters transform data and have at least one input and one output.


Point-based transformation
==========================

Binarization
------------

.. gobj:class:: binarize

    Binarizes an image.

    .. gobj:prop:: threshold:float

        Any values above the threshold are set to one all others to zero.


Clipping
--------

.. gobj:class:: clip

    Clip input to set minimum and maximum value.

    .. gobj:prop:: min:float

        Minimum value, all values lower than `min` are set to `min`.

    .. gobj:prop:: max:float

        Maximum value, all values higher than `max` are set to `max`.


Masking
-------

.. gobj:class:: mask

    Mask the circular outer region by setting values to zero.


Arithmetic expressions
----------------------

.. gobj:class:: calculate

    Calculate an arithmetic expression. You have access to the value stored in
    the input buffer via the *v* letter in :gobj:prop:`expression` and to the
    index of *v* via letter *x*. Please be aware that *v* is a floating point
    number while *x* is an integer. This is useful if you have multidimensional
    data and want to address only one dimension. Let's say the input is two
    dimensional, 256 pixels wide and you want to fill the x-coordinate with *x*
    for all respective y-coordinates (a gradient in x-direction). Then you can
    write *expression="x % 256"*. Another example is the *sinc* function which
    you would calculate as *expression="sin(v) / x"* for 1D input.
    For more complex math or other operations please consider using
    :ref:`OpenCL <generic-opencl-ref>`.

    .. gobj:prop:: expression:string

        Arithmetic expression with math functions supported by OpenCL.


Statistics
----------

.. gobj:class:: measure

    Measure basic image properties.

    .. gobj:prop:: metric:string

        Metric, one of ``min``, ``max``, ``sum``, ``mean``, ``var``, ``std``,
        ``skew`` or ``kurtosis``.

    .. gobj:prop:: axis:int

        Along which axis to measure (-1, all).


.. _generic-opencl-ref:


Generic OpenCL
--------------

.. gobj:class:: opencl

    Load an arbitrary OpenCL :gobj:prop:`kernel` from :gobj:prop:`filename` or
    :gobj:prop:`source` and execute it on each input. The kernel must accept as
    many global float array parameters as connected to the filter and one
    additional as an output. For example, to compute the difference between two
    images, the kernel would look like::

        kernel void difference (global float *a, global float *b, global float *c)
        {
            size_t idx = get_global_id (1) * get_global_size (0) + get_global_id (0);
            c[idx] = a[idx] - b[idx];
        }

    and could be used like so if defined in a file named ``diff.cl``::

        $ ufo-launch [read, read] ! opencl kernel=difference filename=diff.cl !  null

    If :gobj:prop:`filename` is not set, a default kernel file (``opencl.cl``)
    is loaded. See :ref:`opencl-default-kernels` for a list of kernel names
    defined in that file.

    .. gobj:prop:: filename:string

        Filename with kernel sources to load.

    .. gobj:prop:: source:string

        String with OpenCL kernel code.

    .. gobj:prop:: kernel:string

        Name of the kernel that this filter is associated with.

    .. gobj:prop:: options:string

        OpenCL build options.

    .. gobj:prop:: dimensions:uint

        Number of dimensions the kernel works on. Must be in [1, 3].


Spatial transformation
======================

Transposition
-------------

.. gobj:class:: transpose

    Transpose images from (x, y) to (y, x).


Rotation
--------

.. gobj:class:: rotate

    Rotates images clockwise by an :gobj:prop:`angle` around a :gobj:prop:`center`
    (x, y).  When :gobj:prop:`reshape` is ``True``, the rotated image is not
    cropped, i.e.  the output image size can be larger that the input size.
    Moreover, this mode makes sure that the original coordinates of the input are
    all contained in the output so that it is easier to see the rotation in the
    output.  Try e.g.  rotation with :gobj:prop:`center` equal to :math:`(0, 0)` and
    angle :math:`\pi / 2`.

    .. gobj:prop:: angle:float

        Rotation angle in radians.

    .. gobj:prop:: reshape:boolean

        Reshape the result to encompass the complete input image and input
        indices.

    .. gobj:prop:: center:GValueArray

        Center of rotation (x, y)

    .. gobj:prop:: addressing-mode:enum

        Addressing mode specifies the behavior for pixels falling outside the
        original image. See OpenCL ``sampler_t`` documentation for more information.

    .. gobj:prop:: interpolation:enum

        Specifies interpolation when a computed pixel coordinate falls between
        pixels, can be `nearest` or `linear`.


Flipping
--------

.. gobj:class:: flip

    Flips images vertically or horizontally.

    .. gobj:prop:: direction:enum

        Can be either `horizontal` or `vertical` and denotes the direction along
        with the image is flipped.


Binning
-------

.. gobj:class:: bin

    Bin a square of pixels by summing their values.

    .. gobj:prop:: size:uint

        Number of pixels in one direction to bin to a single pixel value.


Rescaling
---------

.. gobj:class:: rescale

    Rescale input data by a fixed :gobj:prop:`factor`.

    .. gobj:prop:: factor:float

        Fixed factor for scaling the input in both directions.

    .. gobj:prop:: x-factor:float

        Fixed factor for scaling the input width.

    .. gobj:prop:: y-factor:float

        Fixed factor for scaling the input height.

    .. gobj:prop:: width:uint

        Fixed width, disabling scalar rescaling.

    .. gobj:prop:: height:uint

        Fixed height, disabling scalar rescaling.

    .. gobj:prop:: interpolation:enum

        Interpolation method used for rescaling which can be either ``nearest`` or ``linear``.


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

    .. gobj:prop:: width:uint

        Width of the padded image.

    .. gobj:prop:: height:uint

        Height of the padded image.

    .. gobj:prop:: addressing-mode:enum

        Addressing mode specifies the behavior for pixels falling outside the
        original image. See OpenCL ``sampler_t`` documentation for more information.


Cropping
--------

.. gobj:class:: crop

    Crop a region of interest from two-dimensional input. If the region is
    (partially) outside the input, only accessible data will be copied.

    .. gobj:prop:: x:uint

        Horizontal coordinate from where to start the ROI.

    .. gobj:prop:: y:uint

        Vertical coordinate from where to start the ROI.

    .. gobj:prop:: width:uint

        Width of the region of interest.

    .. gobj:prop:: height:uint

        Height of the region of interest.

    .. gobj:prop:: from-center:boolean

        Start cropping from the center outwards.


Cutting
-------

.. gobj:class:: cut

    Cuts a region from the input and merges the two halves together. In a way,
    it is the opposite of crop.

    .. gobj:prop:: width:uint

        Width of the region to cut out.


Tiling
------

.. gobj:class:: tile

    Cuts input into multiple tiles. The stream contains tiles in a zig-zag
    pattern, i.e. the first tile starts at the top left corner of the input goes
    on the same row until the end and continues on the first tile of the next
    row until the final tile in the lower right corner.

    .. gobj:prop:: width:uint

        Width of a tile which must be a divisor of the input width. If this is
        not changed, the full width will be used.

    .. gobj:prop:: height:uint

        Width of a tile which must be a divisor of the input height. If this is
        not changed, the full height will be used.


Swapping quadrants
------------------

.. gobj:class:: swap-quadrants

    Cuts the input into four quadrants and swaps the lower right with the upper
    left and the lower left with the upper right quadrant.


Polar transformation
--------------------

.. gobj:class:: polar-coordinates

    Transformation between polar and cartesian coordinate systems.

    When transforming from cartesian to polar coordinates the origin is in the
    image center (:gobj:prop:`width` / 2, :gobj:prop:`height` / 2).  When
    transforming from polar to cartesian coordinates the origin is in the image
    corner (0, 0).

    .. gobj:prop:: width:uint

        Final width after transformation.

    .. gobj:prop:: height:uint

        Final height after transformation.

    .. gobj:prop:: direction: string

        Conversion direction from ``polar_to_cartesian``.



Stitching
---------

.. gobj:class:: stitch

    Stitches two images horizontally based on their relative given
    :gobj:prop:`shift`, which indicates how much is the second image shifted
    with respect to the first one, i.e. there is an overlapping region given by
    :math:`first\_width - shift`. First image is inserted to the stitched image
    from its left edge and the second image is inserted after the overlapping
    region. If shift is negative, the two images are swapped and stitched as
    described above with shift made positive.

    If you are stitching a 360-degree off-centered tomographic data set and
    know the axis of rotation, shift can be computed as :math:`2axis -
    second\_width` for the case the axis of rotation is greater than half of the
    first image. If it is less, then the shift is :math:`first\_width - 2 axis`.
    Moreover, you need to horizontally flip one of the images because this task
    expects images which can be stitched directly, without additional needed
    transformations.

    Stitching requires two inputs. If you want to stitch a 360-degree
    off-centered tomographic data set you can use:

    .. code-block:: bash

        ufo-launch [read path=projections_left/, read path=projections_right/ ! flip direction=horizontal] ! stitch shift=N ! write filename=foo.tif

    .. gobj:prop:: shift:int

        How much is second image shifted with respect to the first one. For
        example, shift 0 means that both images overlap perfectly and the
        stitching doesn't actually broaden the image. Shift corresponding to
        image width makes for a stitched image with twice the width of the
        respective images (if they have equal width).

    .. gobj:prop:: adjust-mean:boolean

        Compute the mean of the overlapping region in the two images and adjust
        the second image to match the mean of the first one.

    .. gobj:prop:: blend:boolean

        Linearly interpolate between the two images in the overlapping region.


Multi-stream
============

Interpolation
-------------

.. gobj:class:: interpolate

    Interpolates incoming data from two compatible streams, i.e.  the task
    computes :math:`(1 - \alpha) s_1 + \alpha s_2` where :math:`s_1` and
    :math:`s_2` are the two input streams and :math:`\alpha` a blend factor.
    :math:`\alpha` is :math:`i / (n - 1)` for :math:`n > 1`, :math:`n` being
    :gobj:prop:`number` and :math:`i` the current iteration.

    .. gobj:prop:: number:uint

        Number of total output stream length.


.. gobj:class:: interpolate-stream

    Interpolates between elements from an incoming stream.

    .. gobj:prop:: number:uint

        Number of total output stream length.


Subtract
--------

.. gobj:class:: subtract

    Subtract data items of the second from the first stream.


Correlate
---------

.. gobj:class:: correlate-stacks

    Reads two datastreams, the first must provide a 3D stack of images that is
    used to correlate individal 2D images from the second datastream. The
    ``number`` property must contain the expected number of items in the second
    stream.

    .. gobj:prop:: number:uint

        Number of data items in the second data stream.


Filters
=======

Median
------

.. gobj:class:: median-filter

    Filters input with a simple median.

    .. gobj:prop:: size:uint

        Odd-numbered size of the neighbouring window.


Edge detection
--------------

.. gobj:class:: detect-edge

    Detect edges by computing the power gradient image using different edge
    filters.

    .. gobj:prop:: filter:enum

        Edge filter (or operator) which is one of ``sobel``, ``laplace`` and
        ``prewitt``. By default, the ``sobel`` operator is used.



Gaussian blur
-------------

.. gobj:class:: blur

    Blur image with a gaussian kernel.

    .. gobj:prop:: size:uint

        Size of the kernel.

    .. gobj:prop:: sigma:float

        Sigma of the kernel.


Gradient
--------

.. gobj:class:: gradient

    Compute gradient.

    .. gobj:prop:: direction:enum

         Direction of the gradient, can be either ``horizontal``, ``vertical``,
         ``both`` or ``both_abs``.


Non-local-means denoising
-------------------------

.. gobj:class:: non-local-means

    Reduce noise using Buades' non-local means algorithm.

    .. gobj:prop:: search-radius:uint

        Radius for similarity search.

    .. gobj:prop:: patch-radius:uint

        Radius of patches.

    .. gobj:prop:: sigma:float

        Sigma influencing the Gaussian weighting.


Horizontal interpolation
------------------------

.. gobj:class:: horizontal-interpolate

    Interpolate masked values in rows of an image. For all pixels equal to one
    in the mask, find the closest pixel where mask is zero to the left and right
    and linearly interpolate the value in the current pixel based on the found
    left and right values. If the mask goes to the left or right border of the
    image and on the other side there are at least two non-masked pixels
    :math:`x_1` and :math:`x_2`, compute the value in the current pixel
    :math:`x` by (in case the mask goes to the right border, left is analogous)
    :math:`f(x) = f(x_2) + (x - x_2) * (f(x_2) - f(x_1))`. In case there is only
    one valid pixel on one of the borders and all the others are masked, use
    that pixel's value in all the remaining ones.


Stream transformations
======================

Averaging
---------

.. gobj:class:: average

    Read in full data stream and generate an averaged output.

    .. gobj:prop:: number:uint

        Number of averaged images to output. By default one image is generated.


Reducing with OpenCL
--------------------

.. gobj:class:: opencl-reduce

    Reduces or folds the input stream using a generic OpenCL kernel by loading
    an arbitrary :gobj:prop:`kernel` from :gobj:prop:`filename` or
    :gobj:prop:`source`. The kernel must accept exactly two global float arrays,
    one for the input and one for the output. Additionally a second
    :gobj:prop:`finish` kernel can be specified which is called once when the
    processing finished. This kernel must have two arguments as well, the global
    float array and an unsigned integer count. Folding (i.e. setting the initial
    data to a known value) is enabled by setting the :gobj:prop:`fold-value`.

    Here is an OpenCL example how to compute the average::

        kernel void sum (global float *in, global float *out)
        {
            size_t idx = get_global_id (1) * get_global_size (0) + get_global_id (0);
            out[idx] += in[idx];
        }

        kernel void divide (global float *out, uint count)
        {
            size_t idx = get_global_id (1) * get_global_size (0) + get_global_id (0);
            out[idx] /= count;
        }

    And this is how you would use it with ``ufo-launch``::

        ufo-launch ... ! opencl-reduce kernel=sum finish=divide ! ...

    If :gobj:prop:`filename` is not set, a default kernel file is loaded. See
    :ref:`opencl-reduction-default-kernels` for a list of possible kernels.

    .. gobj:prop:: filename:string

        Filename with kernel sources to load.

    .. gobj:prop:: source:string

        String with OpenCL kernel code.

    .. gobj:prop:: kernel:string

        Name of the kernel that is called on each iteration. Must have two
        global float array arguments, the first being the input, the second the
        output.

    .. gobj:prop:: finish:string

        Name of the kernel that is called at the end after all iterations. Must
        have a global float array and an unsigned integer arguments, the first
        being the data, the second the iteration counter.

    .. gobj:prop:: fold-value:float

        If given, the initial data is filled with this value, otherwise the
        first input element is used.

    .. gobj:prop:: dimensions:uint

        Number of dimensions the kernel works on. Must be in [1, 3].

Statistics
----------

.. gobj:class:: flatten

    Flatten input stream by reducing with operation based on the given mode.

    .. gobj:prop:: mode:string

        Operation, can be either ``min``, ``max``, ``sum`` and ``median``.

.. gobj:class:: flatten-inplace

    Faster inplace operating variant of the ``flatten`` task.

    .. gobj:prop:: mode:enum

         Operation, can be either ``min``, ``max`` and ``sum``.


Slicing
-------

.. gobj:class:: slice

    Slices a three-dimensional input buffer to two-dimensional slices.


Stacking
--------

.. gobj:class:: stack

    Symmetrical to the slice filter, the stack filter stacks two-dimensional
    input.

    .. gobj:prop:: number:uint

        Number of items, i.e. the length of the third dimension.


Merging
-------

.. gobj:class:: merge

    Merges the data from two or more input data streams into a single data
    stream by concatenation.

    .. gobj:prop:: number:uint

        Number of input streams. By default this is two.


Slice mapping
-------------

.. gobj:class:: map-slice

    Lays out input images on a quadratic grid. If the :gobj:prop:`number` of
    input elements is not the square of some integer value, the next higher
    number is chosen and the remaining data is blackened.

    .. gobj:prop:: number:uint

        Number of expected input elements. If more elements are sent to the
        mapper, warnings are issued.


Color mapping
-------------

.. gobj:class:: map-color

    Receives a two-dimensional image and maps its gray values to three red,
    green and blue color channels using the Viridis color map.


Splitting channels
------------------

.. gobj:class:: unsplit

    Turns a three-dimensional image into two-dimensional image by interleaving
    the third dimension, i.e. [[[XXX],[YYY],[ZZZ]]] is turned into
    [[XYZ],[XYZ],[XYZ]]. This is useful to merge a separate multi-channel RGB
    image into a "regular" RGB image that can be shown with ``cv-show``.

    This task adds the ``channels`` key to the output buffer containing the
    original depth of the input buffer.


Fourier domain
==============

Fast Fourier transform
----------------------

.. gobj:class:: fft

    Compute the Fourier spectrum of input data. If :gobj:prop:`dimensions` is one
    but the input data is 2-dimensional, the 1-D FFT is computed for each row.

    .. gobj:prop:: auto-zeropadding:boolean

        Automatically zeropad input data to a size to the next power of 2.

    .. gobj:prop:: dimensions:uint

        Number of dimensions in [1, 3].

    .. gobj:prop:: size-x:uint

        Size of FFT transform in x-direction.

    .. gobj:prop:: size-y:uint

        Size of FFT transform in y-direction.

    .. gobj:prop:: size-z:uint

        Size of FFT transform in z-direction.


.. gobj:class:: ifft

    Compute the inverse Fourier of spectral input data. If
    :gobj:prop:`dimensions` is one but the input data is 2-dimensional, the 1-D
    FFT is computed for each row.

    .. gobj:prop:: dimensions:uint

        Number of dimensions in [1, 3].

    .. gobj:prop:: crop-width:int

        Width to crop output.

    .. gobj:prop:: crop-height:int

        Height to crop output.


Frequency filtering
-------------------

.. gobj:class:: filter

    Computes a frequency filter function and multiplies it with its input,
    effectively attenuating certain frequencies.

    .. gobj:prop:: filter :enum

        Any of ``ramp``, ``ramp-fromreal``, ``butterworth``, ``faris-byer``,
        ``hamming`` and ``bh3`` (Blackman-Harris-3). The default filter is
        ``ramp-fromreal`` which computes a correct ramp filter avoiding offset
        issues encountered with naive implementations.

    .. gobj:prop:: scale:float

        Arbitrary scale that is multiplied to each frequency component.

    .. gobj:prop:: cutoff:float

        Cutoff frequency of the Butterworth filter.

    .. gobj:prop:: order:float

        Order of the Butterworth filter.

    .. gobj:prop:: tau:float

        Tau parameter of Faris-Byer filter.

    .. gobj:prop:: theta:float

        Theta parameter of Faris-Byer filter.


Stripe filtering
----------------

.. gobj:class:: filter-stripes

    Filter vertical stripes. The input and output are in 2D frequency domain.
    The filter multiplies horizontal frequencies (for frequency ky=0) with a
    Gaussian profile centered at 0 frequency.

    Example usage::

        $ ufo-launch read path=sino.tif ! fft dimensions=2 ! filter-stripes sigma=1 ! ifft dimensions=2 ! write filename=sino-filtered.tif

    .. gobj:prop:: sigma:float

        Filter strength, which is the sigma of the gaussian. Small values, e.g.
        1e-7 cause only the zero frequency to remain in the signal, i.e.
        stronger filtering.


1D stripe filtering
-------------------

.. gobj:class:: filter-stripes1d

    Filter stripes in 1D along the x-axis. The input and output are in frequency
    domain. The filter multiplies the frequencies with an inverse Gaussian
    profile centered at 0 frequency. The inversed profile means that the filter
    is f(k) = 1 - gauss(k) in order to suppress the low frequencies.

    .. gobj:prop:: strength:float

        Filter strength, which is the full width at half maximum of the
        gaussian.


Zeropadding
-----------

.. gobj:class:: zeropad

    Add zeros in the center of sinogram using :gobj:prop:`oversampling`
    to manage the amount of zeros which will be added.

    .. gobj:prop:: oversampling:uint

        Oversampling coefficient.

    .. gobj:prop:: center-of-rotation:float

        Center of rotation of sample.


Reconstruction
==============

Flat-field correction
---------------------

.. gobj:class:: flat-field-correct

    Computes the flat field correction using three data streams:

    1. Projection data on input 0
    2. Dark field data on input 1
    3. Flat field data on input 2

    .. gobj:prop:: absorption-correct:boolean

        If *TRUE*, compute the negative natural logarithm of the
        flat-corrected data.

    .. gobj:prop:: fix-nan-and-inf:boolean

        If *TRUE*, replace all resulting NANs and INFs with zeros.

    .. gobj:prop:: sinogram-input:boolean

        If *TRUE*, correct only one line (the sinogram), thus darks are flats are 1D.

    .. gobj:prop:: dark-scale:float

        Scale the dark field prior to the flat field correct.


Sinogram transposition
----------------------

.. gobj:class:: transpose-projections

    Read a stream of two-dimensional projections and output a stream of
    transposed sinograms. :gobj:prop:`number` *must* be set to the
    number of incoming projections to allocate enough memory.

    .. gobj:prop:: number:uint

        Number of projections.

    .. Warning::

        This is a memory intensive task and can easily exhaust your
        system memory. Make sure you have enough memory, otherwise the process
        will be killed.


Tomographic backprojection
--------------------------

.. gobj:class:: backproject

    Computes the backprojection for a single sinogram.

    .. gobj:prop:: num-projections:uint

        Number of projections between 0 and 180 degrees.

    .. gobj:prop:: offset:uint

        Offset to the first projection.

    .. gobj:prop:: axis-pos:double

        Position of the rotation axis in horizontal pixel dimension of a
        sinogram or projection. If not given, the center of the sinogram is
        assumed.

    .. gobj:prop:: angle-step:double

        Angle step increment in radians. If not given, pi divided by height
        of input sinogram is assumed.

    .. gobj:prop:: angle-offset:double

        Constant angle offset in radians. This determines effectively the
        starting angle.

    .. gobj:prop:: mode:enum

        Reconstruction mode which can be either ``nearest`` or ``texture``.

    .. gobj:prop:: roi-x:uint

        Horizontal coordinate of the start of the ROI. By default 0.

    .. gobj:prop:: roi-y:uint

        Vertical coordinate of the start of the ROI. By default 0.

    .. gobj:prop:: roi-width:uint

        Width of the region of interest. The default value of 0 denotes full
        width.

    .. gobj:prop:: roi-height:uint

        Height of the region of interest. The default value of 0 denotes full
        height.


Forward projection
------------------

.. gobj:class:: forwardproject

    Computes the forward projection of slices into sinograms.

    .. gobj:prop:: number:uint

        Number of final 1D projections, that means height of the sinogram.

    .. gobj:prop:: angle-step:float

        Angular step between two adjacent projections. If not changed, it is
        simply pi divided by :gobj:prop:`number`.


Laminographic backprojection
----------------------------

.. gobj:class:: lamino-backproject

    Backprojects parallel beam computed laminography projection-by-projection
    into a 3D volume.

    .. gobj:prop:: region-values:int

        Elements in regions.

    .. gobj:prop:: float-region-values:float

        Elements in float regions.

    .. gobj:prop:: x-region:GValueArray

        X region for reconstruction as (from, to, step).

    .. gobj:prop:: y-region:GValueArray

        Y region for reconstruction as (from, to, step).

    .. gobj:prop:: z:float

        Z coordinate of the reconstructed slice.

    .. gobj:prop:: region:GValueArray

        Region for the parameter along z-axis as (from, to, step).

    .. gobj:prop:: projection-offset:GValueArray

        Offset to projection data as (x, y) for the case input data is cropped
        to the necessary range of interest.

    .. gobj:prop:: center:GValueArray

        Center of the volume with respect to projections (x, y), (rotation
        axes).

    .. gobj:prop:: overall-angle:float

        Angle covered by all projections (can be negative for negative steps in
        case only num-projections is specified)

    .. gobj:prop:: num-projections:uint

        Number of projections.

    .. gobj:prop:: tomo-angle:float

        Tomographic rotation angle in radians (used for acquiring projections).

    .. gobj:prop:: lamino-angle:float

        Absolute laminogrpahic angle in radians determining the sample tilt.

    .. gobj:prop:: roll-angle:float

        Sample angular misalignment to the side (roll) in radians (CW is
        positive).

    .. gobj:prop:: parameter:enum

        Which paramter will be varied along the z-axis, from ``z``, ``x-center``,
        ``lamino-angle``, ``roll-angle``.


Fourier interpolation
---------------------

.. gobj:class:: dfi-sinc

    Computes the 2D Fourier spectrum of reconstructed image using 1D Fourier
    projection of sinogram (fft filter must be applied before).  There are no
    default values for properties, therefore they should be assigned manually.

    .. gobj:prop:: kernel-size:uint

        The length of kernel which will be used in
        interpolation.

    .. gobj:prop:: number-presampled-values:uint

        Number of presampled values which will be used to calculate
        ``kernel-size`` kernel coefficients.

    .. gobj:prop:: roi-size:int

        The length of one side of region of Interest.

    .. gobj:prop:: angle-step:double

        Increment of angle in radians.


Center of rotation
------------------

.. gobj:class:: center-of-rotation

    Compute the center of rotation of input sinograms.

    .. gobj:prop:: angle-step:double

        Step between two successive projections.

     .. gobj:prop:: center:double

        The calculated center of rotation.


Sinogram offset shift
---------------------

.. gobj:class:: cut-sinogram

    Shifts the sinogram given a center not centered to the input image.

    .. gobj:prop:: center-of-rotation:float

        Center of rotation of specimen.


Phase retrieval
---------------

.. gobj:class:: retrieve-phase

    Computes and applies a fourier filter to correct phase-shifted data.
    Expects frequencies as an input and produces frequencies as an output.

    .. gobj:prop:: method:enum

        Retrieval method which is one of ``tie``, ``ctf``, ``qp`` or ``qp2``.

    .. gobj:prop:: energy:float

        Energy in keV.

    .. gobj:prop:: distance:float

        Distance in meters.

    .. gobj:prop:: pixel-size:float

        Pixel size in meters.

    .. gobj:prop:: regularization-rate:float

        Regularization parameter is log10 of the constant to be added to the
        denominator to regularize the singularity at zero frequency: 1/sin(x) ->
        1/(sin(x)+10^-RegPar). It is also log10(delta / beta) where the complex
        refractive index is delta + beta * 1j.

        Typical values [2, 3].

    .. gobj:prop:: thresholding-rate:float

        Parameter for Quasiparticle phase retrieval which defines the width of
        the rings to be cropped around the zero crossing of the CTF denominator
        in Fourier space.

        Typical values in [0.01, 0.1], ``qp`` retrieval is rather independent of
        cropping width.

    .. gobj:prop:: frequency-cutoff:float

        Cutoff frequency after which the filter is set to 0 in radians.

    .. gobj:prop:: output-filter:boolean

        Output filter values instead of the filtered frequencies.


General matrix-matrix multiplication
====================================

.. gobj:class:: gemm

    Computes :math:`\alpha A \cdot B + \beta C` where :math:`A`, :math:`B` and :math:`C` are input
    streams 0, 1 and 2 respectively. :math:`A` must be of size :math:`m\times k`, :math:`B`
    :math:`k\times n` and :math:`C` :math:`m\times n`.

    .. note::

        This filter is only available if CLBlast support is available.

    .. gobj:prop:: alpha:float

        Scalar multiplied with :math:`AB`.

    .. gobj:prop:: beta:float

        Scalar multiplied with :math:`C`.


Segmentation
============

.. gobj:class:: segment

    Segments a stack of images given a field of labels using the random walk
    algorithm described in  [#]_. The first
    input stream must contain three-dimensional image stacks, the second input
    stream a label image with the same width and height as the images. Any pixel
    value other than zero is treated as a label and used to determine segments
    in all directions.

    .. [#]
        Lösel and Heuveline, *Enhancing a Diffusion Algorithm for 4D Image
        Segmentation Using Local Information* in Proc. SPIE 9784, Medical
        Imaging 2016, http://proceedings.spiedigitallibrary.org/proceeding.aspx?articleid=2506235


Auxiliary
=========

Buffering
---------

.. gobj:class:: buffer

    Buffers items internally until data stream has finished. After that all
    buffered elements are forwarded to the next task.

    .. gobj:prop:: number:uint

        Number of pre-allocated buffers.

    .. gobj:prop:: dup-count:uint

        Number of times each image should be duplicated.

    .. gobj:prop:: loop:boolean

        Duplicates the data in a loop manner :gobj:prop:`dup-count` times.


Stamp
-----

.. gobj:class:: stamp

    Writes the current iteration into the top-left corner.

    .. gobj:prop:: font:string

        Pango font description, by default set to ``Mono 9``.

    .. gobj:prop:: scale:float

        Scales the default brightness of 1.0.

    .. note::

        This filter requires Pango and Cairo for text layouting.


Loops
-----

.. gobj:class:: loop

    Repeats output of incoming data items. It uses a low-overhead policy to
    avoid unnecessary copies. You can expect the data items to be on the device
    where the data originated.

    .. gobj:prop:: number:uint

        Number of iterations for each received data item.


Monitoring
----------

.. gobj:class:: monitor

    Inspects a data stream and prints size, location and associated metadata
    keys on stdout.

    .. gobj:prop:: print:uint

        If set print the given numbers of items on stdout as hexadecimally
        formatted numbers.


Sleep
-----

.. gobj:class:: sleep

    Wait :gobj:prop:`time` seconds before continuing. Useful for debugging
    throughput issues.

    .. gobj:prop:: time:double

        Time to sleep in seconds.


Display
-------

.. gobj:class:: cv-show

    Shows the input using an OpenCV window.

    .. gobj:prop:: min:float

        Minimum for display value scaling. If not set, will be determined at
        run-time.

    .. gobj:prop:: max:float

        Maximum for display value scaling. If not set, will be determined at
        run-time.
