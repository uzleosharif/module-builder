


# modi


I usually have small c++ modules to build. Setting up cmake is an overkill and 
brings unnecessary complexity. With this tool, I want to streamline my c++ module 
development.

This projects provides a utility `modi` that could be used to setup other modules.
Running `modi` at c++ project root (containing build.json)
emits a `build.ninja` file in build/ dir.
`ninja` can be invoked then to generate BMI, static/shared-library and
native executable.

## format for build.json


The `build.json` file now allows **explicit per-source dependency specification** for fine-grained control of module builds.

```
{
  "b": "build_dir",    // [optional] build output folder (default: "build/")

  "src": {
    "bar.cppm": [],                // module interface, no dependencies
    "baz.cppm": [],                // another module interface, no dependencies
    "main.cpp": ["bar.cppm", "baz.cppm"]   // main.cpp depends on bar.cppm and baz.cppm
  },

  "l": ["sodium"],      // [optional] system-libs to link against

  "e": "binary_name"    // build executable `binary_name`
  // or
  // "a": "some_lib_name"  // build static lib `libsome_lib_name.a`
  // or 
  // "so": "some_lib_name" // build shared-object lib `libsome_lib_name.so`
}

```

## module registry

The tool relies on a module registry on file-system at `/modules/`.
To get such a dev environment for free, it is recommended to use 
`cpp-modules-base` docker as provided in `dockers/cpp-modules-base/` folder.


## possible future work

I am thinking but not yet convinced if i need to provide `install` support. As I always think, 
simply copying/moving the artifacts around should suffice most of local dev use-cases.
