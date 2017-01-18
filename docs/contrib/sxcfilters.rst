===============================
Contributions by Serge X. Cohen
===============================

These filters were initially written with X-ray tomographic processing
in mind. Still they are of a general usage as long as input are image.


Point-based transformation
==========================

Rejecting outliers in 3D
------------------------

.. gobj:class:: med-mad-reject

    For each pixel of a frame within a stream, makes a 3x3x3 box (that
    is, 3x3 box including previous, current and following frames) and
    compute the median (med) and median absolute deviation (mad). If
    the value of the central pixel is too far from the median
    (relative to the mad) it is rejected and its value is replaced by
    the median value of the box.

    .. gobj:prop:: threshold:float

        When abs(px-med) > threshold*mad the pixel value (noted px)
        is replaced by med.


Rejecting outliers in 2D
------------------------

.. gobj:class:: med-mad-reject-2d

    For each pixel of a frame make a square box centred on the pixel and
    compute the median (med) and median absolute deviation (mad). If
    the value of the central pixel is too far from the median
    (relative to the mad) it is rejected and its value is replaced by
    the median value of the box.

    .. gobj:prop:: box-size:uint

        The edge size of the box to be used (in px). This should be an
        even number so that it can be centred on a pixel.

    .. gobj:prop:: threshold:float

        When abs(px-med) > threshold*mad the pixel value (noted px)
        is replaced by med.

OpenCL one-liner computation
----------------------------

.. gobj:class:: ocl-1liner

   The aim is to enable the implementation of simple, nevertheless
   multiple input, computation on the basis of one work-item per pixel
   of the frame. The filter accepts arbitrary number of inputs, as
   long as more than one is provided. The output has the same size as
   the first input (indexed 0) and the generated OpenCL kernel is run
   with one work item per pixel of the output. The user provides a
   single computation line and the filter places it within a skeleton
   to produce an OpenCL kernel on the fly, then compiles it and uses
   it in the current workflow.

   In the kernel the following variables are defined :
   `sizeX` and `sizeY` are the size of the frame in X and Y directions;
   `x` and `y` are the coordinate of the pixel corresponding to the
   current work item;
   `in_x` are the buffer holding the 0..(n-1) input frames;
   `out` is the buffer holding the output of the computation.

   In the computation line provided through :gobj:prop:`one-line` the
   pixel corresponding to the current work item is `px_index`. Also
   reference to the pixel values can use multiple syntax :
   `out[px_index]`, `in_0[px_index]`, ... `in_x[px_index]` or as
   shortcut (indeed macro of those) `out_px`, `in_0_px`,
   ... `in_x_px`. Finally if one wants to have finer control over the
   pixel used in the computation (being able to use neighbouring pixel
   values) one can use the `IMG_VAL` macro as such `IMG_VAL(x,y,out)`,
   `IMG_VAL(x,y,in_x)` ...

   .. gobj:prop:: one-line:string

       The computation to be performed expressed in one line of
       OpenCL, no trailing semi-column (added by the skeleton). To
       avoid miss-interpretation of the symbols by the line parser of
       ufo-launch it is advisable to surround the line by single
       quotes (on top of shell quoting). One example (invoking through
       ufo-launch) would be `"'out_px = (in_0_px > 0) ? sqrt(in_0_px)
       : 0.0f'"` .

   .. gobj:prop:: num-inputs:uint
       
       The number of input streams. This is mandatory since it can not
       be inferred as it is the case by the :ref:`OpenCL
       <generic-opencl-ref>` task.

   .. gobj:prop:: quiet:boolean

       Default to `true`, when set to `false` the dynamically
       generated kernel sources are printed to the standard output
       during the task setup.


Auxiliary
=========

Producing simple statistics on a stream
---------------------------------------

.. gobj:class:: stat-monitor

    Inspects a data stream in a way similar to the :gobj:class:`monitor`
    task but also computing simple statistics on the monitored frame stream:
    min, max, mean and standard deviation of each frame is computed. To limit
    truncation errors the OpenCL kernel uses fp64 operations if those are
    supported by the used OpenCL device, otherwise it falls back to use fp32
    arithmetic which might incurs significant truncation errors on images of
    large dimensions.

    .. gobj:prop:: filename:string

        When provided the tabulated statistics are output the file
        with this filename rather than displayed to standard output.

    .. gobj:prop:: trace:boolean

        When set to `true` will print processed frame index to
        standard output. This is useful if the task is placed in before
        a task somehow hiding the number of processed frames (in a
        complex workflow). Defaulting to `false`

    .. gobj:prop:: quiet:boolean

        When set to `true` will not print the frame
        monitoring. Defaulting to `false` to be as close as possible
        to the output of the :gobj:class:`monitor` task.

    .. gobj:prop:: print:uint

        If set print the given numbers of items on stdout as hexadecimally
        formatted numbers (taken from :gobj:class:`monitor` task).

