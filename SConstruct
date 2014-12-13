#!/usr/bin/python

import os
import sys

options = Variables('options.cache', ARGUMENTS)
options.AddVariables(PathVariable('uv', 'Location of libuv', '../sys/libuv'))
options.AddVariables(PathVariable('v8', 'Location of v8', '../sys/v8'))

VariantDir('build', 'src', duplicate=0)
kwargs = {}
if sys.platform == 'darwin':
	kwargs['CXX'] = 'clang++'

env = Environment(options=options, **kwargs)
v8 = env['v8']
uv = env['uv']
env.Append(CPPPATH=[
	os.path.join(v8, 'include'),
	v8,
	os.path.join(uv, 'include'),
])
if sys.platform == 'win32':
	env.Append(LIBS=['v8_base', 'v8_libbase', 'v8_libplatform', 'v8_nosnapshot', 'icui18n', 'icuuc', 'libuv', 'advapi32', 'winmm', 'wsock32', 'ws2_32', 'psapi', 'iphlpapi'])
	env.Append(CXXFLAGS=['/EHsc', '/MTd', '/Zi'])
	env.Append(LIBPATH=[
		os.path.join(v8, 'build/Debug/lib'),
		os.path.join(uv, 'Debug/lib'),
	])
	env.Append(LINKFLAGS=['/DEBUG'])
elif sys.platform == 'darwin':
	env.Append(LIBS=['v8_base', 'v8_libbase', 'v8_libplatform', 'v8_nosnapshot', 'icui18n', 'icuuc', 'icudata', 'pthread', 'uv'])
	env.Append(CXXFLAGS=['--std=c++11', '-g', '-Wall', '-stdlib=libstdc++'])
	env.Append(LINKFLAGS=['-g', '-stdlib=libstdc++'])
	env.Append(LIBPATH=[
		os.path.join(v8, 'out/x64.release'),
		os.path.join(uv, 'out/Debug'),
	])
else:
	env.Append(LIBS=['v8_base', 'v8_libbase', 'v8_libplatform', 'v8_nosnapshot', 'icui18n', 'icuuc', 'icudata', 'pthread', 'uv', 'rt'])
	env.Append(CXXFLAGS=['--std=c++0x', '-g', '-Wall'])
	env.Append(LINKFLAGS=['-g'])
	env.Append(LIBPATH=[
		os.path.join(v8, 'out/native/obj.target/third_party/icu'),
		os.path.join(v8, 'out/native/obj.target/tools/gyp'),
		os.path.join(uv, 'out/Debug/obj.target'),
	])
env.Program('sandboxos', Glob('build/*.cpp'))
