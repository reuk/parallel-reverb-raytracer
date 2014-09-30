parallel raytrace
=================

A raytracer for impulse responses (for reverb), influenced by raster graphics 
lighting techniques. Uses OpenCL for IMMENSE SPEED.

to do
-----

* Check that the intersection calculations are actually working properly.
    * Export some data to visualise.

* Integrate with the realtime convolution.
    * Try to make realtime(ish) maybe

* Use hrtf
    * Write a speedy hrtf calculator
        * Probably multiple parallel discrete convolutions?
