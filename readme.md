parallel raytrace
=================

A raytracer for impulse responses (for reverb), influenced by raster graphics 
lighting techniques. Uses OpenCL for IMMENSE SPEED.

to do
-----

* Integrate with the realtime convolution.
    * Try to make realtime(ish) maybe

* Use hrtf
    * Write a speedy hrtf calculator
        * Probably multiple parallel discrete convolutions?

* Add multiple-source processing

* Consider faster intersection-calcluation algorithms

done
----

* Check that the intersection calculations are actually working properly.
    * Export some data to visualise.

to think about
--------------

* If I'm going to do this in anything approaching to real-time, I'll need an
  opencl queue that I push source and mic positions to, as these are the
  only things that will be allowed to change.

* As long as the speaker setup doesn't change, I can keep the same post-
  processing code.

* As long as I queue everything, I should be able to do other stuff on the main
  thread, and just block whenever I need to get a new ir from the generator.

