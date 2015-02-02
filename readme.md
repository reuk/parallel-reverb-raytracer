parallel raytrace
=================

A raytracer for impulse responses (for reverb), influenced by raster graphics
lighting techniques. Uses OpenCL for IMMENSE SPEED.

build notes
-----------

Only tested on Mac OS X Mavericks and Yosemite.

You need the following libraries installed in a standard location (probably /usr/local/lib, /usr/local/include) so that CMake can find and link them:
* FFTW (fftw3f)
* Assimp
* Libsndfile

Once these libraries all have CMake builds of their own I'll add git submodules for them.

On Mac OS X the OpenCL framework is used. Obviously this isn't something that Linuxes have, so at some point I'll try making the build a bit more platform agnostic, to see if it will run on Linux as well.

to do
-----

* Throw an error if an object is invalid / contains invalid materials.

* Consider faster intersection-calcluation algorithms
    * Might actually end up slower due to increased divergence

* Add a proper testing framework
    * Aim for coverage of all the library-visible functions first
    * Then for coverage of internal functions if time

* low frequency estimation?
    * check limiting factors
    * look for different approaches
        * or not :P

* look into different filtering/band-splitting methods
    * check crossover of two-pass biquad filters

* Add a (filtered) direct image of the source to the output

* image source stuff
    * backwards visiblity checks
    * keep track of valid paths

* check high frequency content - sounds a bit dull
    * might just be my test model

* generate a whole bunch of sample reverbs
    * build a max patch for auditioning them
