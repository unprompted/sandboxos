# SandboxOS
SandboxOS is a program that aims to securely host pure JavaScript web applications.

## Goals
1. Make it easy to run all sorts of servers and web applications.
2. Provide a security model that is easy to understand and protects your data.
3. Make creating and sharing web applications accessible to anyone from a web interface.

## Building
SandboxOS is [routinely](https://www.unprompted.com/projects/build/sandboxos) built on Linux, Windows, and OS X.

1. Get and build [Google V8](https://code.google.com/p/v8-wiki/wiki/UsingGit) (latest 3.30).
2. Get and build [libuv](https://github.com/libuv/libuv) (latest 1.0).
3. Run:
  ```
  scons uv=path/to/libuv v8=path/to/v8
  ```

## Running
Running the built sandbox executable will start a web server.  This is a good starting point: <http://localhost:12345/tasks>.

To grant users administrator privileges, create a JSON file named data/auth/permissions.js containing a list of users and their permissions, like this:
```
{
  "cory": ["administrator"]
}
```

This is a work in progress.  Everything else can be managed entirely from the web interface.

## License
All code unless otherwise noted in [COPYING](https://github.com/unprompted/sandboxos/blob/master/COPYING) is provided under the [Affero GPL 3.0](https://github.com/unprompted/sandboxos/blob/master/LICENSE) license.
