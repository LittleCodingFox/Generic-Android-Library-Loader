Generic-Android-Library-Loader
==============================

Generic Library Loader for Android NDK

Instructions
============

GALL uses two metadata values on your `AndroidManifest.xml` to load library dependencies and a target library.

The metadata is named `gall.dependencies` for dependencies, which is in the form of `"a|b|c"`, and `gall.target` which is the name of the target library to load
