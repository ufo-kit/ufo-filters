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
