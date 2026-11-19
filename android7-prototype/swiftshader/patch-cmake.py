"""Patch the build copy of SwiftShader's CMakeLists.txt for Android 7 (API 24). Every substitution must match.
Same adaptation as the Android 10 route (android10-system-r4-prototype/research/build_swiftshader.py), except:
no libnativewindow and no libsync (Android 8+; the API 24 window and gralloc0 paths need neither).
  python3 patch-cmake.py path/to/CMakeLists.txt"""
import sys
from pathlib import Path

p = Path(sys.argv[1]); t = p.read_text()
def sub(old, new, count=1):
    global t
    n = t.count(old)
    if n != count: sys.exit('patch-cmake: %r expected %d match(es), found %d' % (old[:60], count, n))
    t = t.replace(old, new)

# Android instead of X11: the Android framebuffer/gralloc sources, real Android logging.
sub('set_cpp_flag("-DUSE_X11=1")', 'set_cpp_flag("-D__ANDROID__=1")')
sub('''        ${SOURCE_DIR}/Main/FrameBufferX11.cpp
        ${SOURCE_DIR}/Main/FrameBufferX11.hpp''', '''        ${SOURCE_DIR}/Common/GrallocAndroid.cpp
        ${SOURCE_DIR}/Common/GrallocAndroid.hpp
        ${SOURCE_DIR}/Main/FrameBufferAndroid.cpp''')
sub('''        ${SOURCE_DIR}/Main/libX11.cpp
        ${SOURCE_DIR}/Main/libX11.hpp''', '', count=2)  # Reactor and SwiftShader source lists
sub('list(REMOVE_ITEM SWIFTSHADER_LIST\n    ${SOURCE_DIR}/Common/DebugAndroid.cpp\n    ${SOURCE_DIR}/Common/DebugAndroid.hpp\n    ${SOURCE_DIR}/Common/GrallocAndroid.cpp\n    ${SOURCE_DIR}/Common/GrallocAndroid.hpp\n)',
    'list(REMOVE_ITEM SWIFTSHADER_LIST ${SOURCE_DIR}/Common/Debug.cpp)')
sub('    ${OPENGL_DIR}/common/Object.cpp', '    ${OPENGL_DIR}/common/Image.cpp\n    ${OPENGL_DIR}/common/Object.cpp')
sub('target_link_libraries(libEGL ${OS_LIBS})', 'target_link_libraries(libEGL SwiftShader ${Reactor} GLCompiler ${OS_LIBS})')
sub('set(OS_LIBS dl pthread)', 'set(OS_LIBS dl log utils hardware cutils c++ m c)')
# Distinct SONAMEs: never shadow Android's own libEGL/libGLESv2 dispatch libraries.
for target, name in (('libEGL', 'libEGL_swiftshader'), ('libGLESv2', 'libGLESv2_swiftshader'), ('libGLES_CM', 'libGLESv1_CM_swiftshader')):
    sub('set_target_properties(' + target + ' PROPERTIES', 'set_target_properties(' + target + ' PROPERTIES OUTPUT_NAME ' + name)
# The LLVM 7 library is only used by the LLVM Reactor backend; with Subzero it is never linked (its 462 MB source
# tree is not copied into the build), so it is not defined at all.
sub("add_library(llvm STATIC ${LLVM_LIST})", 'if(${REACTOR_BACKEND} STREQUAL "LLVM")\nadd_library(llvm STATIC ${LLVM_LIST})')
sub("""if(LINUX)
    target_link_libraries(llvm dl)
endif(LINUX)""", """if(LINUX)
    target_link_libraries(llvm dl)
endif(LINUX)
endif()""")
# Subzero's LLVM support headers: the Android configuration.
t = t.replace('${SUBZERO_LLVM_DIR}/build/Linux/include', '${SUBZERO_LLVM_DIR}/build/Android/include')
p.write_text(t)
print('patch-cmake: OK')
