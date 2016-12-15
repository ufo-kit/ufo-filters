# -*- coding: utf-8 -*-

import sys
import os
import time
import re


sys.path.insert(0, os.path.abspath('.'))


def get_version():
    patterns = [
            r'^set\(UFO_FILTERS_VERSION_MAJOR "(\d*)"\)',
            r'^set\(UFO_FILTERS_VERSION_MINOR "(\d*)"\)',
            r'^set\(UFO_FILTERS_VERSION_PATCH "(\d*)"\)'
            ]
    version = ["0", "0", "0"]

    with open('../CMakeLists.txt', 'r') as f:
        lines = f.readlines()
        major_pattern = r'^set\(UFO_FILTERS_VERSION_MAJOR "(\d*)"\)'

        for line in lines:
            for i, pattern in enumerate(patterns):
                m = re.match(pattern, line)

                if m:
                    version[i] = m.group(1)

    return '.'.join(version)


project = u'UFO Tasks'
copyright = u'%s, UFO Collaboration' % time.strftime('%Y')
version = get_version()
release = version

extensions = ['sphinx.ext.mathjax', 'sphinxgobject']
templates_path = ['_templates']

master_doc = 'index'
source_suffix = '.rst'

exclude_patterns = ['_build']

pygments_style = 'sphinx'

html_theme = 'default'
html_use_smartypants = True
htmlhelp_basename = 'ufo-filtersdoc'


latex_documents = [
  ('index', 'ufo-filters.tex', u'UFO Tasks Reference',
   u'Matthias Vogelgesang', 'manual'),
]
