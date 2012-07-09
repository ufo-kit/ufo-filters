import os
import unittest
import scipy.misc
import numpy as np
from libtiff import TIFF
from gi.repository import Ufo, GLib


def write_lena(path):
    tif = TIFF.open(path, mode='w')
    tif.write_image(scipy.misc.lena().astype(np.float32))


class UfoTestCase(unittest.TestCase):
    def setUp(self):
        fp = 'UFO_FILTER_PATH'

        if fp in os.environ:
            paths = os.environ[fp]
            self.g = Ufo.Graph(paths=paths)
            self.pm = Ufo.PluginManager(paths=paths)
        else:
            self.g = Ufo.Graph()
            self.pm = Ufo.PluginManager()

    def assertEqualDelta(self, a, b, delta):
        assert(abs(a - b) < delta)
