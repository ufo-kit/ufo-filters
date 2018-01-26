========
Examples
========

CT Pre-processing
=================

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
:gobj:class:`flatten` with the mode set to ``median``. Suppose, we want to
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

If you want to avoid the automatic absorption correction you have to set
``absorption-correct`` to FALSE and if you want to ignore NaN and Inf values in
the data, set ``fix-nan-and-inf`` to FALSE.


Sinograms
---------

The reconstruction pipelines presented in the following section assume sinograms
as input in order to parallelize along slices. To transpose a stream of
(corrected) projections connect it to :gobj:class:`transpose-projections` and
set ``number`` to the number of expected projections. Note, that the
transposition happens in main memory and thus may exhaust your system resources
for a larger number of big projections. For example, to transpose 2048
projections, each at a size of 2048 by 2048 pixels requires 32 GB of RAM.


CT Reconstruction
=================

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


Data distribution
=================

To distribute data in a compute network you can use the :gobj:class:`zmq-pub`
sink and :gobj:class:`zmq-sub` generator. For example, to read data on machine A
and store it on machine B, you would run

.. code-block:: bash

    ufo-launch read path=/data ! zmq-pub

on machine A and

.. code-block:: bash

    ufo-launch zmq-sub address=tcp://hostname-of-machine-a ! write

on machine B. Note that by default :gobj:class:`zmq-pub` publishes data as soon
as it receives it, thus some of the data will get lost if the
:gobj:class:`zmq-sub` is run after :gobj:class:`zmq-pub`. You can prevent this
by telling the :gobj:class:`zmq-pub` task to wait for a certain number of
subscribers to subscribe:

.. code-block:: bash

    ufo-launch read path=/data ! zmq-pub expected-subscribers=1


.. rubric:: References

.. [KaSl01] Kak, A. C., & Slaney, M. (2001). Principles of Computerized Tomographic Imaging (Philadelphia, PA: SIAM).
