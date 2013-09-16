from distutils.core import setup, Extension

setup( name =    'pyid3lib',
       version = '0.5.1',
       description = 'read and edit ID3 tags on MP3 files (using id3lib)',
       
       author = 'Doug Zongker',
       author_email = 'dzongker@sourceforge.net',
       url = 'http://pyid3lib.sourceforge.net/',

       ext_modules = [Extension( 'pyid3lib',
                                 ['pyid3lib.cc'],
                                 libraries=['stdc++','id3','z'] )]
       )

       
