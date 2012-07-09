import numpy as np
import os
import ufotest
import unittest
from libtiff import TIFF


class TestWriteEqual(ufotest.UfoTestCase):
    def test_write_equal(self):
        src = 'lena.tif'
        dst = 'foo-00000.tif'

        ufotest.write_lena(src)

        rd = self.pm.get_filter('reader')
        wr = self.pm.get_filter('writer')
        rd.set_properties(path=src)
        wr.set_properties(path='.', prefix='foo-')

        self.g.connect_filters(rd, wr)
        self.g.run()

        original = TIFF.open(src, mode='r').read_image()
        written = TIFF.open(dst, mode='r').read_image()

        d = np.sum(np.abs(original - written))
        self.assertEqual(d, 0.0)

        os.remove(src)
        os.remove(dst)


if __name__ == '__main__':
    unittest.main()
