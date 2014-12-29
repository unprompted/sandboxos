#!/usr/bin/python

import os
import sys

options = Variables('options.cache', ARGUMENTS)
options.AddVariables(PathVariable('uv', 'Location of libuv', '../sys/libuv'))
options.AddVariables(PathVariable('v8', 'Location of v8', '../sys/v8'))
options.AddVariables(BoolVariable('package', 'Build a package', False))

VariantDir('build/src', 'src', duplicate=0)
VariantDir('build/deps', 'deps', duplicate=0)
kwargs = {}
if sys.platform == 'darwin':
	kwargs['CXX'] = 'clang++'

env = Environment(options=options, tools=['default', 'packaging'], **kwargs)
options.Save('options.cache', env)
Help(options.GenerateHelpText(env))

v8 = env['v8']
uv = env['uv']
env.Append(CPPPATH=[
	os.path.join(v8, 'include'),
	v8,
	os.path.join(uv, 'include'),
	os.path.join('deps', 'liblmdb'),
])
if sys.platform == 'win32':
	env.Append(LIBS=['v8_base', 'v8_libbase', 'v8_libplatform', 'v8_nosnapshot', 'icui18n', 'icuuc', 'libuv', 'advapi32', 'winmm', 'wsock32', 'ws2_32', 'psapi', 'iphlpapi'])
	env.Append(CXXFLAGS=['/EHsc', '/MTd', '/Zi', '/Gy'])
	env.Append(CFLAGS=['/EHsc', '/MTd', '/Zi', '/Gy'])
	env.Append(LIBPATH=[
		os.path.join(v8, 'build/Debug/lib'),
		os.path.join(uv, 'Debug/lib'),
	])
	env.Append(LINKFLAGS=['/DEBUG', '/OPT:REF', '/OPT:ICF'])
elif sys.platform == 'darwin':
	env.Append(LIBS=['v8_base', 'v8_libbase', 'v8_libplatform', 'v8_nosnapshot', 'icui18n', 'icuuc', 'icudata', 'pthread', 'uv'])
	env.Append(CXXFLAGS=['--std=c++11', '-g', '-Wall', '-stdlib=libstdc++'])
	env.Append(CFLAGS=['-g', '-Wall'])
	env.Append(LINKFLAGS=['-g', '-stdlib=libstdc++'])
	env.Append(LIBPATH=[
		os.path.join(v8, 'out/x64.release'),
		os.path.join(uv, 'out/Debug'),
	])
else:
	env.Append(LIBS=['v8_base', 'v8_libbase', 'v8_libplatform', 'v8_nosnapshot', 'icui18n', 'icuuc', 'icudata', 'pthread', 'uv', 'rt'])
	env.Append(CXXFLAGS=['--std=c++0x', '-g', '-Wall'])
	env.Append(CFLAGS=['-g', '-Wall'])
	env.Append(LINKFLAGS=['-g'])
	env.Append(LIBPATH=[
		os.path.join(v8, 'out/native/obj.target/third_party/icu'),
		os.path.join(v8, 'out/native/obj.target/tools/gyp'),
		os.path.join(uv, 'out/Debug/obj.target'),
	])

ldapEnv = env.Clone()
if sys.platform == 'win32':
	ldapEnv.Append(CPPPATH=['deps/win32'])
lmdb = ldapEnv.Library('build/lmdb', [
	'build/deps/liblmdb/mdb.c',
	'build/deps/liblmdb/midl.c',
])
env.Append(LIBS=[lmdb])

if sys.platform == 'linux2':
	sslEnv = env.Clone()
	sslEnv.Append(CPPPATH=[
		'deps/libressl',
		'deps/libressl/crypto',
		'deps/libressl/crypto/asn1',
		'deps/libressl/crypto/evp',
		'deps/libressl/crypto/md2',
		'deps/libressl/crypto/modes',
		'deps/libressl/crypto/store',
		'deps/libressl/include',
	])

	sslSources = Glob('build/deps/libressl/ssl/*.c') + \
		Glob('build/deps/libressl/crypto/*.c') + \
		Glob('build/deps/libressl/crypto/*/*.c') + \
		Glob('build/deps/libressl/engines/*.c')

	def buildSslSource(fileName):
		basename = os.path.basename(fileName)
		parentDirectory = os.path.basename(os.path.dirname(fileName))
		return basename not in ('b_win.c', 'ui_openssl_win.c', 'apps_win.c', 'poll_win.c', 'chacha-merged.c', 'poly1305-donna.c') \
			and not parentDirectory in ('compat',)

	sslSources = [s for s in sslSources if buildSslSource(str(s))]
	sslSources += [
		'build/deps/libressl/crypto/compat/arc4random.c',
		'build/deps/libressl/crypto/compat/explicit_bzero.c',
		'build/deps/libressl/crypto/compat/issetugid_linux.c',
		'build/deps/libressl/crypto/compat/getentropy_linux.c',
		'build/deps/libressl/crypto/compat/reallocarray.c',
		'build/deps/libressl/crypto/compat/strlcat.c',
		'build/deps/libressl/crypto/compat/strlcpy.c',
		'build/deps/libressl/crypto/compat/timingsafe_memcmp.c',
	]
	lssl = sslEnv.Library('build/libressl', sslSources, CPPDEFINES=['OPENSSL_NO_HW_PADLOCK'])
	env.Append(LIBS=[lssl])

	sslCliSources = Glob('build/deps/libressl/apps/*.c')
	sslCliSources = [s for s in sslCliSources if buildSslSource(str(s))]
	sslEnv.Program('openssl-cli', sslCliSources, LIBS=[lssl],
		CPPDEFINES=['HAVE_POLL', 'OPENSSL_NO_HW_PADLOCK'])

env.Program('sandboxos', Glob('build/src/*.cpp'))

def listAllFiles(root):
	for root, dirs, files in os.walk(root):
		for f in files:
			if not f.startswith('.'):
				yield os.path.join(root, f)
		hidden = [d for d in dirs if d.startswith('.')]
		for d in hidden:
			dirs.remove(d)

if env['package'] and sys.platform == 'win32':
	files = [
		'COPYING',
		'LICENSE',
		'SConstruct',
		'sandboxos.exe',
		'sandboxos.pdb',
	]
	files += listAllFiles('src')
	files += listAllFiles('packages')
	env.Package(
		NAME='SandboxOS',
		target='dist/SandboxOS-win32.zip',
		PACKAGETYPE='zip',
		PACKAGEROOT='SandboxOS-win32',
		source=files
	)
