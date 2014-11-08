#!/usr/bin/python

import os

options = Variables('options.cache', ARGUMENTS)
options.AddVariables(PathVariable('uv', 'Location of libuv', '../sys/libuv'))
options.AddVariables(PathVariable('v8', 'Location of v8', '../sys/v8'))

VariantDir('build', 'src', duplicate=0)
env = Environment(options=options)
v8 = env['v8']
uv = env['uv']
env.Append(CPPPATH=[
	os.path.join(v8, 'include'),
	v8,
	os.path.join(uv, 'include'),
])
env.Append(LIBS=['v8_base', 'v8_libbase', 'v8_libplatform', 'v8_nosnapshot', 'icui18n', 'icuuc', 'icudata', 'pthread', 'uv', 'rt'])
env.Append(LIBPATH=[
	os.path.join(v8, 'out/native/obj.target/third_party/icu'),
	os.path.join(v8, 'out/native/obj.target/tools/gyp'),
	os.path.join(uv, 'out/Debug/obj.target'),
])
env.Append(CXXFLAGS=['--std=c++0x', '-g'])
env.Append(LINKFLAGS=['-g'])
env.Program('sandboxos', Glob('build/*.cpp'))
