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
        def ignore_message(domain, level, message, user):
            pass

        GLib.log_set_handler("Ufo", GLib.LogLevelFlags.LEVEL_MASK,
                ignore_message, None)

        GLib.log_set_handler("GLib-GObject", GLib.LogLevelFlags.LEVEL_MASK,
                ignore_message, None)

        fp = 'UFO_FILTER_PATH'
        if fp in os.environ:
            self.g = Ufo.Graph(paths=os.environ[fp])
        else:
            self.g = Ufo.Graph()

    def assertEqualDelta(self, a, b, delta):
        assert(abs(a - b) < delta)
