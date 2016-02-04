from distutils.core import setup, Extension

clipmodule = Extension('clip',
		sources = ['clipmodule.cpp'],
		libraries = ['polyclipping'])

setup(name = 'clip',
		version = '0.1',
		description = 'Internal module for gerber handling',
		ext_modules = [clipmodule])
