import numpy as np
import os
import ufotest
import unittest
from libtiff import TIFF


class TestFFT(ufotest.UfoTestCase):
    def test_fft_1d(self):
        src = 'lena.tif'
        dst = 'foo-00000.tif'

        ufotest.write_lena(src)

        rd = self.pm.get_filter('reader')
        fft = self.pm.get_filter('fft')
        ifft = self.pm.get_filter('ifft')
        wr = self.pm.get_filter('writer')

        rd.set_properties(path=src)
        wr.set_properties(path='.', prefix='foo-')
        fft.set_properties(dimensions=1)
        ifft.set_properties(dimensions=1)

        self.g.connect_filters(rd, fft)
        self.g.connect_filters(fft, ifft)
        self.g.connect_filters(ifft, wr)
        self.g.run()

        original = TIFF.open(src, mode='r').read_image()
        written = TIFF.open(dst, mode='r').read_image()

        m = np.mean(np.abs(original - written))
        self.assertEqualDelta(m, 0.0, 0.00001)

        os.remove(src)
        os.remove(dst)

    def test_fft_2d(self):
        src = 'lena.tif'
        dst = 'foo-00000.tif'

        ufotest.write_lena(src)

        rd = self.pm.get_filter('reader')
        fft = self.pm.get_filter('fft')
        ifft = self.pm.get_filter('ifft')
        wr = self.pm.get_filter('writer')

        rd.set_properties(path=src)
        wr.set_properties(path='.', prefix='foo-')
        fft.set_properties(dimensions=2)
        ifft.set_properties(dimensions=2)

        self.g.connect_filters(rd, fft)
        self.g.connect_filters(fft, ifft)
        self.g.connect_filters(ifft, wr)
        self.g.run()

        original = TIFF.open(src, mode='r').read_image()
        written = TIFF.open(dst, mode='r').read_image()

        m = np.mean(np.abs(original - written))
        self.assertEqualDelta(m, 0.0, 0.0001)

        os.remove(src)
        os.remove(dst)

if __name__ == '__main__':
    unittest.main()
