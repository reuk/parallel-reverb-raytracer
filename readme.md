parallel raytrace
=================

A raytracer for impulse responses (for reverb), influenced by raster graphics 
lighting techniques. Uses OpenCL for IMMENSE SPEED.

to do
-----

* Throw an error if an object is invalid / contains invalid materials.

* Consider faster intersection-calcluation algorithms
    * Might actually end up slower due to increased divergence

* Use hrtf
    * Write a speedy hrtf calculator
        * Probably multiple parallel discrete convolutions?

* Add multiple-source processing
    * Can probably do this by just running the process several times
    * Keep all the geometry loaded, to reduce slowdown

* Add a proper testing framework
    * Aim for coverage of all the library-visible functions first
    * Then for coverage of internal functions if time
    * Add a sample .obj to the project for testing/loading purposes
    * Add a function to the Scene class to facilitate validation of scene
      data

* Consider distance-based attennuation

maybe
-----

* Integrate with realtime convolution.
    * Try to make realtime(ish) maybe
    * The realtime convolution still needs threading somehow

* Consider multiple diffuse coefficients

done
----

* Check that the intersection calculations are actually working properly.
    * Export some data to visualise.

* Modify the .obj so that it has proper materials.
    * The .obj has an accompanying .mtl which contains material defs
    * You can modify this by hand to use the correct materials
    * Just make sure you have MATCHING materials where you need them when
      building the .obj
    * Then modify them yourself after exporting

* Check how assimp deals with unit testing on obj files - relative paths?
    * Then do the same thing obviously
    * For the time being, there's just a .obj file bundled in the main dir
    * This is copied to the runtime output dir, and preprocessor macros
      are used to direct the tests and proof-of-concept app to the correct loc.

to think about
--------------

* If I'm going to do this in anything approaching to real-time, I'll need an
  opencl queue that I push source and mic positions to, as these are the
  only things that will be allowed to change.

* As long as the speaker setup doesn't change, I can keep the same post-
  processing code.

* As long as I queue everything, I should be able to do other stuff on the main
  thread, and just block whenever I need to get a new ir from the generator.

