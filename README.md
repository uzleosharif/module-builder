


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


The `build.json` file lists the sources that make up a module project.
`modi` figures out the dependencies between these sources by inspecting their
`import` statements, so specifying them manually is no longer required.

```
{
  "b": "build_dir",    // [optional] build output folder (default: "build/")

  "src": ["bar.cppm", "baz.cppm", "main.cpp"], // list of source files need to build project

  "imp": ["bar", ...], // list of c++ modules that need to be imported

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
