


# modi


I usually have small c++ modules to build. Setting up cmake is an overkill and 
brings unnecessary complexity. With this tool, I want to streamline my c++ module 
development.

This projects provides a utility `modi` that could be used to setup other modules.
Running `modi` at c++ project root (containing build.json)
emits a `build.ninja` file in build/ dir.
`ninja` can be invoked then to generate BMI, static/shared-library and
native executable.

