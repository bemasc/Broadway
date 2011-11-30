#!/usr/bin/python

import os, sys, re, json, shutil
from subprocess import Popen, PIPE, STDOUT

exec(open(os.path.expanduser('~/.emscripten'), 'r').read())

sys.path.append(EMSCRIPTEN_ROOT)
import tools.shared as emscripten

EMSCRIPTEN_SETTINGS = {
  'USE_TYPED_ARRAYS': 2,
  'I64_MODE': 1,
  'CORRECT_ROUNDINGS': 0,
# Emscripten cannot currently trace correct-sign requirements across
# multiple libraries linked together, and the decoder produces garbage
# with CORRECT_SIGNS=0, so we have no choice but to enable
# correct signs across the board.
  'CORRECT_SIGNS': 1,
#  'CORRECT_SIGNS_LINES': emscripten.read_pgo_data('vp8.pgo')['signs_lines'],
  'CORRECT_OVERFLOWS': 0, # Setting this to 1 seems to improve correctness only slightly
  'CORRECT_SIGNED_OVERFLOWS': 0,
  'CHECK_SIGNS': 0,
  'CHECK_OVERFLOWS': 0,
  'CHECK_SIGNED_OVERFLOWS': 0,
  'SAFE_HEAP': 0,
  'SAFE_HEAP_LOG': 0,
  'INVOKE_RUN': 0, # we do it ourselves
  'EXPORTED_FUNCTIONS': ['_main'],
  'IGNORED_FUNCTIONS': ['_paint'],
  'RELOOP': 1, # Unbelievably slow.  Set this to 0 during development.
  'MICRO_OPTS': 1,
  'PGO': 0
}
DEPENDENCIES = ['libvpx/libvpx_g.a','nestegg/src/.libs/libnestegg.a.bc']
EMSCRIPTEN_ARGS = [] #Emscripten's optimize (-O) appears to be broken

JS_DIR = "js"

if not os.path.exists(JS_DIR):
  os.makedirs(JS_DIR)

print 'Build'

env = os.environ.copy()
env['CC'] = env['CXX'] = env['RANLIB'] = env['AR'] = emscripten.EMMAKEN
env['LINUX'] = '1'
env['EMMAKEN_CFLAGS'] = '-U__APPLE__ -DJS -Inestegg/include -Ilibvpx'

Popen(['make', '-j', '4'], env=env).communicate()

if 0:
  print 'LLVM optimizations'

  shutil.move('vp8.bc', 'vp8.orig.bc')
  output = Popen([emscripten.LLVM_OPT, 'vp8.orig.bc'] +
                 emscripten.Building.pick_llvm_opts(1, safe=True) +
                 ['-o=vp8.bc']).communicate()[0]
  assert os.path.exists('vp8.bc'), 'Failed to run llvm optimizations: ' + output

print 'Linking LLVM library with its dependencies'
shutil.move('vp8.bc', 'vp8.orig.bc')

cmd = ['/home/bens/vp8/local/bin/llvm-ld', '-link-as-library', '-disable-opt', 'vp8.orig.bc'] + DEPENDENCIES + ['-o=vp8.bc']
print ' '.join(cmd)
print Popen(cmd).communicate()
#print Popen([emscripten.LLVM_LINK] + ['vp8.orig.bc', '-o=vp8.bc'] + DEPENDENCIES).communicate()

print 'LLVM binary => LL assembly'

print ' '.join([emscripten.LLVM_DIS] + emscripten.LLVM_DIS_OPTS + ['vp8.bc', '-o=vp8.ll'])
print Popen([emscripten.LLVM_DIS] + emscripten.LLVM_DIS_OPTS + ['vp8.bc', '-o=vp8.ll']).communicate()

if 0:
  print '[Autodebugger]'

  shutil.move('vp8.ll', 'vp8.orig.ll')
  output = Popen(['python', emscripten.AUTODEBUGGER, 'vp8.orig.ll', 'vp8.ll'], stdout=PIPE, stderr=STDOUT).communicate()[0]
  assert 'Success.' in output, output

  shutil.move('vp8.bc', 'vp8.orig.bc')
  print Popen([emscripten.LLVM_AS, 'vp8.ll', '-o=vp8.bc']).communicate()

print 'Emscripten: LL assembly => JavaScript'

settings = ['-s %s=%s' % (k, json.dumps(v)) for k, v in EMSCRIPTEN_SETTINGS.items()]

filename = JS_DIR + '/vp8.js'

print Popen(['python', os.path.join(EMSCRIPTEN_ROOT, 'emscripten.py')] + EMSCRIPTEN_ARGS + ['vp8.ll'] + settings,#  ).communicate()
            stdout=open(filename, 'w'), stderr=STDOUT).communicate()

print 'Appending stuff'

src = open(filename, 'a')

if 0: #EMSCRIPTEN_SETTINGS['QUANTUM_SIZE'] == 1:
  src.write(
    '''
      _malloc = function(size) {
        while (STATICTOP % 4 != 0) STATICTOP++;
        var ret = STATICTOP;
        STATICTOP += size;
        return ret;
      }
    '''
  )

if 0: # Console debugging
  src.write(
    '''
      _paint = _SDL_Init = _SDL_LockSurface = _SDL_UnlockSurface = function() {
      };

      _SDL_SetVideoMode = function() {
        return _malloc(1024);
      };

      FS.createDataFile('/', 'admiral.264', %s, true, false);
      FS.root.write = true;
      print('zz go!');
      run(['admiral.264']);
      print('zz gone');

    ''' % str(map(ord, open('../Media/admiral.264').read()[0:1024*100]))
  )
  # ~/Dev/mozilla-central/js/src/fast/js -m avc.js
else:
  src.write(open('hooks.js').read())
  src.write(open('paint_%s.js' % EMSCRIPTEN_SETTINGS['USE_TYPED_ARRAYS'], 'r').read())
src.close()

if 0:
  print 'Eliminating unneeded variables'
  
  eliminated = Popen([emscripten.COFFEESCRIPT, emscripten.VARIABLE_ELIMINATOR], stdin=PIPE, stdout=PIPE).communicate(open(filename, 'r').read())[0]
  filename = JS_DIR + '/vp8.elim.js'
  f = open(filename, 'w')
  f.write(eliminated)
  f.close()

if 1:
  print 'Closure compiler'

  cmd = ['java', '-jar', emscripten.CLOSURE_COMPILER,
               '--compilation_level', 'SIMPLE_OPTIMIZATIONS', # XXX TODO: use advanced opts for code size (they cause slow startup though)
               '--js', filename, '--js_output_file', JS_DIR + '/vp8.cc.js']
  print(' '.join(cmd))
  Popen(cmd).communicate()
