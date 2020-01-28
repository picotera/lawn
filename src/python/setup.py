from distutils.core import setup

long_description =""

with open("README.rst", "r") as fh:
  long_description = fh.read()

setup(
  name = 'timer_lawn',
  packages = ['lawn'],
  version = '0.1',
  description = 'Low Latancy Timer Data-Structure for Large Scale Systems',
  long_description=long_description,
  author = 'Adam Lev-Libfeld',
  author_email = 'adam@tamarlabs.com',
  license='MIT',
  url = 'https://github.com/picotera/lawn',
  keywords = ['lawn', 'timer', 'timers', 'store', 'HPC', 'latancy'], # arbitrary keywords
  classifiers = [],
)