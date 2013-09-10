import os
import shutil
import tempfile

try:
    import unittest2 as unittest
except ImportError:
    import unittest

import numpy as np
from nose_parameterized import parameterized
from gi.repository import Ufo, GLib
from libtiff import TIFF


def data_path(suffix=''):
    return os.path.join(os.path.abspath('./data'), suffix)

def ref_path(suffix=''):
    return os.path.join(os.path.abspath('./expected'), suffix)

def ignore_message(domain, level, message, user):
    pass

GLib.log_set_handler('Ufo', GLib.LogLevelFlags.LEVEL_DEBUG, ignore_message, None)

class BasicTests(unittest.TestCase):
    def setUp(self):
        self.pm = Ufo.PluginManager()
        self.graph = Ufo.TaskGraph()
        self.sched = Ufo.Scheduler()
        self.tmpdir = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self.tmpdir)

    def tmp_path(self, suffix=''):
        return os.path.join(self.tmpdir, suffix)

    def get_task(self, name, **kwargs):
        plugin = self.pm.get_task(name)
        plugin.set_properties(**kwargs)
        return plugin

    def test_read_write(self):
        reader = self.get_task('reader', path=data_path('sinogram-*.tif'), count=5)
        writer = self.get_task('writer', filename=self.tmp_path('b-%05i.tif'))

        self.graph.connect_nodes(reader, writer)
        self.sched.run(self.graph)

        expected = ['b-%05i.tif' % i for i in xrange(5)]
        found = os.listdir(self.tmpdir)

        for fname in expected:
            self.assertIn(fname, found)

    def test_roi(self):
        R = {'x': 2, 'y': 13, 'width': 256, 'height': 128}
        input_name = data_path('sinogram-00005.tif')
        output_name = self.tmp_path('roi-00000.tif')

        reader = self.get_task('reader', path=input_name)
        writer = self.get_task('writer', filename=self.tmp_path('roi-%05i.tif'))
        roi = self.get_task('region-of-interest', **R)

        self.graph.connect_nodes(reader, roi)
        self.graph.connect_nodes(roi, writer)
        self.sched.run(self.graph)

        ref_img = TIFF.open(input_name, mode='r').read_image()
        res_img = TIFF.open(output_name, mode='r').read_image()

        # Cut it manually
        ref_img = ref_img[R['y']:R['y']+R['height'], R['x']:R['x']+R['width']]

        self.assertEqual(res_img.shape, ref_img.shape)
        self.assertTrue((res_img == ref_img).all())

    @parameterized.expand([(1, 0.5), (2, 0.5)])
    def test_fft(self, dimension, expected):
        input_name = data_path('sinogram-00005.tif')
        output_name = self.tmp_path('r-00000.tif')

        reader = self.get_task('reader', path=input_name)
        writer = self.get_task('writer', filename=self.tmp_path('r-%05i.tif'))
        fft = self.get_task('fft', dimensions=dimension)
        ifft = self.get_task('ifft', dimensions=dimension)

        self.graph.connect_nodes(reader, fft)
        self.graph.connect_nodes(fft, ifft)
        self.graph.connect_nodes(ifft, writer)
        self.sched.run(self.graph)

        ref_img = TIFF.open(input_name, mode='r').read_image()
        res_img = TIFF.open(output_name, mode='r').read_image()
        diff = np.sum(np.abs(ref_img - res_img))
        self.assertLess(diff, expected)

    def test_flatfield_correction(self):
        input_name = data_path('sinogram-*.tif')
        output_name = self.tmp_path('r-%i.tif')
        flat = self.get_task('reader', path=input_name, count=2)
        dark = self.get_task('reader', path=input_name, count=2)
        proj = self.get_task('reader', path=input_name, count=10)
        writer = self.get_task('writer', filename=output_name)

        flat_avg = self.get_task('averager', num_generate=10)
        dark_avg = self.get_task('averager', num_generate=10)
        ffc = self.get_task('flat-field-correction')

        self.graph.connect_nodes(dark, dark_avg)
        self.graph.connect_nodes(flat, flat_avg)
        self.graph.connect_nodes_full(proj, ffc, 0)
        self.graph.connect_nodes_full(dark_avg, ffc, 1)
        self.graph.connect_nodes_full(flat_avg, ffc, 2)
        self.graph.connect_nodes(ffc, writer)
        self.sched.run(self.graph)

    # def test_filtered_backprojection(self):
    #     reader = self.get_task('reader', path=data_path('sinogram*.tif'))
    #     fft = self.get_task('fft')
    #     ifft = self.get_task('ifft')
    #     fltr = self.get_task('filter')
    #     bp = self.get_task('backproject')
    #     writer = self.get_task('writer', filename=self.tmp_path('r-%05i.tif'))

    #     self.graph.connect_nodes(reader, fft)
    #     self.graph.connect_nodes(fft, fltr)
    #     self.graph.connect_nodes(fltr, ifft)
    #     self.graph.connect_nodes(ifft, bp)
    #     self.graph.connect_nodes(bp, writer)
    #     self.sched.run(self.graph)

    #     refs = sorted((ref_path(d) for d in os.listdir(ref_path())))
    #     results = sorted((os.path.join(self.tmpdir, d) for d in os.listdir(self.tmpdir)))

    #     for ref, res in zip(refs, results):
    #         ref_img = TIFF.open(ref, mode='r').read_image()
    #         res_img = TIFF.open(res, mode='r').read_image()
    #         self.assertLess(np.sum(np.abs(res_img - ref_img)), 0.01)
