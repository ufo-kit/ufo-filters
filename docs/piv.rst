===========
PIV filters
===========

Filters related to the PIV particle tracking software.

Ring pattern
------------

.. gobj:class:: ring-pattern

    This generator is used to create all the patterns that one wants to recognize.
    In this case, only ring patterns are generated and the only difference between
    each pattern is it's radii size.  The thickness of the rings stays identical no
    matter the radii size.

    The :gobj:prop:`ring-start` and :gobj:prop:`ring-end` represent the range of radii used for
    the ring generations and are given on a pixel basis.  Each of these rings
    will have a thickness of :gobj:prop:`ring-thickness` pixels. Using a low value for
    the ring thickness tends to result in more rings being detected. Ideally
    this value should be the same as the actual ring thickness within the image.

    .. gobj:prop:: ring-start:uint
    
        Gives the size of the radius of the first ring to be generated. The size
        is given on a pixel basis.

    .. gobj:prop:: ring-step:uint

        Indicates by how much the radii should be increased at each iteration.
        The value is given on a pixel basis.

    .. gobj:prop:: ring-end:uint

        Gives the size of the radius of the last ring to be generated. The size
        is given on a pixel basis.

    .. gobj:prop:: ring-thickness:uint

        Specifies the desired thickness of the generated ring on a pixel basis.

    .. gobj:prop:: width:uint

        Give x size of output image.

    .. gobj:prop:: height:uint

        Give y size of output image.


Concatenate
-----------

.. gobj:class:: concatenate-result


    For each image, there are ``(ring-end - ring-start + 1) / ring-step``
    streams of data. Each stream represents a set of detected rings.  The
    concatenate plugin groups these results into one big stream of ring
    coordinates. This stream is then passed to a set of post processing
    plug-ins that try to remove false positives and find the most accurate ring
    possible.

    Input
        A 1D stream.  This stream represents the list of all the rings detected for a
        certain radius and a certain image.  Of course if their are 10 different
        radii sizes, then 10 calls to the input buffer will result into a single call
        to the output buffer.

    Output
        One list of coordinates, corresponding to all the rings of the current image
        being processed.


    .. gobj:prop:: max-count:uint

        Sometimes for small rings patterns hundreds of rings can be detected due
        to the noise. When large amounts of rings are detected, most of them
        tend to be false positives. To ignore those rings, set the
        ``max-count``. Note that if it is set to a very high value (over 200)
        the post processing algorithms might considerably slow down the
        software.

    .. gobj:prop:: ring-count:uint

        The maximum number of rings desired per ring pattern.


Denoise
-------

.. gobj:class:: denoise

    A temporary background image is computed from the input image.  For each
    pixel in the input image, the neighbouring pixels are loaded into memory and
    then sorted in ascending order. The 30th percentile is then loaded into the
    background image.  The input image is then subtracted by this background
    image.  The advantage of this algorithm is to create a new image whose
    intensity level is homogeneously spread across the whole image.  Indeed, the
    objective here is to remove all background noise and keep the rings whose
    intensities are always higher than the background noise.  This filter later
    helps the Hough Transform because when noise will be summed up, the overall
    value will be close to zero instead of having a high value if we had not
    removed this background noise.

    Input
        A 2D stream. The image taken by the CMOSs camera.

    Output
        A 2D stream.  This plug-in computes an average background image of the
        input.  The output image is then created by subtracting the input image
        to this background image.

    .. gobj:prop:: matrix-size:uint

        This parameter specifies the size of the matrix used when looking for
        neighbouring pixels.  A bigger value for the matrix size means that more
        pixels will be compared at a time.  Ideally, the size should be twice as
        big as the desired ``ring-thickness``. The ring thickness is the number
        of pixels that can be seen on the rings edge.  If the size is identical
        to or less than the effective ring thickness, pixels within rings in the
        image might get removed (i.e. set to 0).


Contrast
--------

.. gobj:class:: contrast

    It has been noticed in an empirical way that the rings always stand in the
    high frequencies of the images, i.e. the pixels with higher intensities.
    Moreover, only a small amount of the pixels, around 10%, form all the rings
    in the image.  Hence a histogram is computed to know where most of the
    pixels stand.  As a general rule, it was noticed that every pixels that are
    below the peak in the histogram are simply background noise.  This is why
    each pixel below this peak is set to 0.  To make the ring stand out a bit
    more a non linear mapping is made to enhance the bright pixels even more.
    By using the `imadjust` algorithm as described in matlab, we compute the new
    pixel values using the following formula : :math:`f'(x, y) =
    \left(\frac{f(x, y)  - low}{high - low}\right)^\gamma` Where :math:`f'` is
    the output image, :math:`f` is the input image, :math:`high` is the maximum value
    and :math:`low` is the smallest value. :math:`\gamma` is a value less than 1, and
    is what allows to get a non linear mapping and more values near the high
    intensities.

    Input
        A 2D stream. The image is the previously denoised image.

    Output
        A 2D stream. All low intensities have been removed and the rings
        contrast has been increased.

    .. gobj:prop:: remove-high:boolean

        When this parameter is set true, every pixel in the histogram that lie
        between half of the distance of the peak and the maximum and the maximum
        value are replaced by a value of 0.  This can be useful when the image
        has lots of bright regions which cause a lot of noise and hence
        generating many false positives.


Ordfilt
-------

.. gobj:class:: ordfilt

    The plug-in  matches a pattern over each pixel of the image and computes a value
    representing the likeliness for that pixel to be the center of that pattern.
    To achieve this, every pixel that lie under the pattern are loaded into memory
    and then sorted.  Once the array is sorted two values are picked to compute the
    rings contrast and the rings average intensities.  Currently we pick the 25th
    and 50th percentile pixel value.  The following formula is then applied to get
    the new pixel value:
    
    .. math::

        contrast = 1 - (high_p - low_p)

        intensity = \frac{(high_p + low_p)}{2} 

        f'(x, y) = intensity \cdot contrast

    :math:`high_p` is the 50 percentile pixel value. :math:`low_p` is the 25th
    percentile pixel value. This formula is based on the fact that rings are always
    brighter, hence the more bright the pixels the more likely we have a ring.
    Moreover,  the pixels forming the ring should not vary in intensity, i.e.
    the low and high percentile should have the same value,  by computing the
    difference we can compute a good contrast value of the ring.  The resulting
    image therefor takes into consideration both the contrast of the ring and
    its intensity.

    Input 1
        A 2D stream. The previously contrasted image.

    Input 2
        A 2D stream. An image representing a pattern to match.  In our case, the
        pattern is a ring.

    Output
        A 2D stream.  An image where each pixel value represents the likeliness
        for that pixel to be the center of the current pattern passed in input1.


Particle filtering
------------------

.. gobj:class:: filter-particle

    This algorithm is based on two-pass method to detect blobs. A blob, is a set
    of bright pixels that form a bright spot on the image.  Each pixel in a blob
    has sufficiently high enough value, based on a threshold, such as that pixel
    is a candidate to being the center of the ring-pattern being currently
    searched for.  For each of these blobs, a unique :math:`(x, y, r)` value is
    computed.  Where :math:`(x, y)` is the center of the blob of pixels and
    :math:`r` is the radius of the current ring-pattern being searched for.

    Input
        A 2D stream. The image generated by the ordfilt, where each pixel value
        represents the likeliness of it to become the center of a ring.

    Output
        A 1D stream. An array of  :math:`(x, y, r)` coordinates representing the list
        of currently detected rings.

    .. gobj:prop:: threshold:float

        A value between 0 and 1 representing a threshold relative to the images
        maximum value.  Each pixel of the image whose value is greater than
        :math:`threshold \cdot \max(Image)` is considered as a candidate to being a
        center of a ring.

    .. gobj:prop:: min:float

        Gives the minimum value a pixels needs to have to be considered a
        possible candidate.


Ring dumping
------------

.. gobj:class:: dump-ring

    .. gobj:prop:: scale:uint

        Says by how much rings should be increased.


get-dup-circ
------------

.. gobj:class:: get-dup-circ

    .. gobj:prop:: threshold:float

        Give maximum ring distance and radius difference.


Circle removing
---------------

.. gobj:class:: remove-circle

    .. gobj:prop:: threshold:float

        Set maximum inner and outer ring radii size difference.


Writing rings
-------------

.. gobj:class:: ringwriter

    .. gobj:prop:: scale:uint

        Says by how much rings should be increased.

    .. gobj:prop:: filename:string

        Path for the output file to write data.
