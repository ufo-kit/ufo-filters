import numpy as np
import os
import ufotest
import ufotools
import unittest
from libtiff import TIFF


class TestBufferInput(ufotest.UfoTestCase):
    def test_buffer_input(self):
        dst = 'foo-00000.tif'
        in_array = np.eye(1024, dtype=np.float32) * 0.5
        in_data = [ufotools.fromarray(in_array)]

        rd = self.pm.get_filter('bufferinput')
        wr = self.pm.get_filter('writer')
        rd.set_properties(buffers=in_data)
        wr.set_properties(path='.', prefix='foo-')

        self.g.connect_filters(rd, wr)
        self.g.run()

        print in_array[0:10,0:10]

        written = TIFF.open(dst, mode='r').read_image()
        print written[0:10,0:10]
        d = np.sum(in_array - written)
        self.assertEqual(d, 0.0)

        os.remove(dst)


if __name__ == '__main__':
    unittest.main()
