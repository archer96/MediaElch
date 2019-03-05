# Third Party dependencies:
#  - QuaZip

quazip_headers_moc = [
  'quazip/quazip/quagzipfile.h',
  'quazip/quazip/quaziodevice.h',
  'quazip/quazip/quazipfile.h'
]

quazip_src = [
  'quazip/quazip/JlCompress.cpp',
  'quazip/quazip/qioapi.cpp',
  'quazip/quazip/quaadler32.cpp',
  'quazip/quazip/quacrc32.cpp',
  'quazip/quazip/quagzipfile.cpp',
  'quazip/quazip/quaziodevice.cpp',
  'quazip/quazip/quazip.cpp',
  'quazip/quazip/quazipdir.cpp',
  'quazip/quazip/quazipfile.cpp',
  'quazip/quazip/quazipfileinfo.cpp',
  'quazip/quazip/quazipnewinfo.cpp',
  'quazip/quazip/unzip.c',
  'quazip/quazip/zip.c',
]

quazip_processed = qt5.preprocess(moc_headers : quazip_headers_moc)
quazip_inc = include_directories('quazip')
quazip_qt5_dep = dependency('qt5', modules : ['Core'])

zlib_dep = dependency('zlib')

if cc.has_argument('-w') == false
    warning('Your compiler does not support \'-w\' to deactivate QuaZip warnings')
endif

quazip_lib = static_library('quazip',
    sources: [quazip_src, quazip_processed],
    include_directories: quazip_inc,
    dependencies: [quazip_qt5_dep, zlib_dep],
    cpp_args: '-w',
    install: false
)
