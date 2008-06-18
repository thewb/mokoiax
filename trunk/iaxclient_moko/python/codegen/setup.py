"""ctypes code generator
"""
from distutils.core import setup


setup(name="ctypes_codegen",
      packages = ['ctypes_codegen'],
      scripts = ['scripts/h2xml.py', 'scripts/xml2py.py'],
      description="ctypes code generator",
      long_description = __doc__,
      author="Thomas Heller",
      author_email="theller@python.net",
      license="MIT License",
##          url="http://starship.python.net/crew/theller/ctypes.html",
      platforms=["windows", "Linux", "MacOS X", "Solaris", "FreeBSD"],
      )
