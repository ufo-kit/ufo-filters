===================
Computed Tomography
===================

Reconstruction
==============

Filtered backprojection
-----------------------

To reconstruct from sinograms using the analytical filtered backproject method
[KaSl01]_, you have to feed the sinograms into :gobj:class:`fft` →
:gobj:class:`filter` → :gobj:class:`ifft` → :gobj:class:`backproject` to obtain
slices one by one::

    ufo-launch \
        dummy-data width=$DETECTOR_WIDTH height=$N_PROJECTIONS number=$N_SLICES ! \
        fft dimensions=1 ! \
        filter ! \
        ifft dimensions=! ! \
        backproject axis-pos=$AXIS ! \
        null


Direct Fourier inversion
------------------------

In this example we use the Fourier slice theorem to obtain slices directly from
projection data [KaSl01]_ and use a sinc kernel to interpolate in the Fourier
space. To reconstruct, you have to feed the sinograms into :gobj:class:`zeropad`
→ :gobj:class:`fft` → :gobj:class:`dfi-sinc` → :gobj:class:`swap-quadrants` →
:gobj:class:`ifft` → :gobj:class:`swap-quadrants`::

    ufo-launch \
        dummy-data width=$DETECTOR_WIDTH height=$N_PROJECTIONS number=$N_SLICES ! \
        zeropad center-of-rotation=$AXIS ! \
        fft dimensions=1 auto-zeropadding=0 ! \
        dfi-sinc ! \
        swap-quadrants ! \
        ifft dimensions=2 ! \
        swap-quadrants ! \
        null


.. rubric:: References

.. [KaSl01] Kak, A. C., & Slaney, M. (2001). Principles of Computerized Tomographic Imaging (Philadelphia, PA: SIAM).
