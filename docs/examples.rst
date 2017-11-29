===================
Computed Tomography
===================

Pre-processing
==============

Flat field correction
---------------------

To remove fixed pattern noise that stems from the optical system caused by
imperfections in the scintillator screen or an inhomogeneous beam and
thermal noise from the detector sensor, you can use *flat field correction*.
This assumes that you have a set of dark fields acquired with the shutter
closed, a set of flat fields acquired without the sample in the beam and the
projections with samples. If the beam intensity shifts over time it can be
beneficial to acquire flat fields before and after the projections and
interpolate between them.

In the simplest case you connect the projection stream to input 0, the dark
field to input 1 and the flat field to input 2 of
:gobj:class:`flat-field-correct`:

.. code-block:: bash

    ufo-launch \
        [ \
            read path=projections*.tif, \
            read path=dark.tif, \
            read path=flat.tif \
        ] ! \
        flat-field-correct !
        write filename=corrected-%05i.tif

If you have a stream of flats and darks you have to reduce them either by
connection them to :gobj:class:`average` or :gobj:class:`stack` →
gobj:class:`flatten` with the mode set to ``median``. Suppose, we want to
average the darks and remove extreme outliers from the flats, we would call

.. code-block:: bash

    ufo-launch \
        [ \
            read path=projections*.tif, \
            read path=darks/ ! average, \
            read path=flats/ ! stack number=11 ! flatten mode=median \
        ] ! \
        flat-field-correct !
        write filename=corrected-%05i.tif

If you have to interpolate between the flats taken before and after the sample
scan, you would connect the first flat to input 0 and the second to input 1 of
:gobj:class:`interpolate` and set the ``number`` property to the number of
expected projections:

.. code-block:: bash

    ufo-launch \
        [ \
            read path=projections*.tif, \
            read path=darks/ ! average, \
            [ \
                read path=flat-before.tif, \
                read path=flat-after.tif \
            ] ! interpolate number=2000
        ] ! \
        flat-field-correct !
        write filename=corrected-%05i.tif


Reconstruction
==============

Filtered backprojection
-----------------------

To reconstruct from sinograms using the analytical filtered backproject method
[KaSl01]_, you have to feed the sinograms into :gobj:class:`fft` →
:gobj:class:`filter` → :gobj:class:`ifft` → :gobj:class:`backproject` to obtain
slices one by one:

.. code-block:: bash

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
:gobj:class:`ifft` → :gobj:class:`swap-quadrants`

.. code-block:: bash

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
