import numpy as np
import os
import ufotest
import unittest
from libtiff import TIFF


class TestWriteRoi(ufotest.UfoTestCase):
    def test_write_roi(self):
        src = 'lena.tif'
        dst = 'foo-00000.tif'

        ufotest.write_lena(src)

        rd = self.pm.get_filter('reader')
        wr = self.pm.get_filter('writer')
        rd.set_properties(path=src, region_of_interest=True, x=32, y=16, width=512-32, height=256)
        wr.set_properties(path='.', prefix='foo-')

        self.g.connect_filters(rd, wr)
        self.g.run()

        original = TIFF.open(src, mode='r').read_image()
        written = TIFF.open(dst, mode='r').read_image()

        d = np.sum(np.abs(original[16:16+256,32:] - written))
        self.assertEqual(d, 0.0)

        os.remove(src)
        os.remove(dst)


if __name__ == '__main__':
    unittest.main()
