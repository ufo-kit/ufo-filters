.. _opencl-default-kernels:

OpenCL default kernels
======================

This section lists all kernel functions that are available to the
:gobj:class:`opencl` filter if no filename is specified.

.. c:function:: void fix_nan_and_inf ()

    Sets element to 0.0 if it is NaN or Inf.

.. c:function:: void absorptivity ()

    Computes :math:`f(x) = -log(x)`.

.. c:function:: void nlm_noise_reduction ()

    Smooths data within a local neighbourhood.

.. c:function:: void diff ()

    Computes :math:`f(x, y) = x - y`.


.. _opencl-reduction-default-kernels:

OpenCL reduction default kernels
================================

This section lists all kernel functions that are available to the
:gobj:class:`opencl-reduce` filter if no filename is specified. These kernels
are supposed to be used for the ``kernel`` argument.

.. c:function:: void minimum ()

    Computes the minimum of each pixel in the stream.

.. c:function:: void maximum ()

    Computes the maximum of each pixel in the stream.

.. c:function:: void sum ()

    Computes the sum of each pixel in the stream.


These kernels are supposed to be used in the ``finish`` argument:

.. c:function:: void divide ()

    Divides each pixel by the stream count. Together with ``sum`` this can be
    used to compute the average, i.e.::

        ufo-launch .. ! opencl-reduce kernel=sum finish=divide ! ..
