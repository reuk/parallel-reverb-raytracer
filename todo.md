To Do
=====

* Consider faster intersection-calcluation algorithms
    * Might actually end up slower due to increased divergence

* low frequency estimation?
    * check limiting factors
    * look for different approaches
        * or not :P

* look into different filtering/band-splitting methods
    * check crossover of two-pass biquad filters

* Add a proper testing framework
    * Aim for coverage of all the library-visible functions first
    * Then for coverage of internal functions if time

* check high frequency content - sounds a bit dull
    * might just be my test model

* where is the cutoff between rays and sound?
    * where are balancing points between other constraints?

[x] diffuse vs image source
[x] no direct impulse
[x] make sure all exceptions have catchers
[x] hipass on output?
[x] check octave bands / highpass limit
[x] filtering methods cmd line option
[x] (no) normalization
[x] constant volume scaling
[x] warning if source or mic are outside bounds of model
[x] constant volume scaling factor in max patch
[x] max patch with ability to choose impulses from a menu
[x] no predelay
[x] direct impulse not collision-tested

[x] power/distance law
    * ignored by rober, kaminski and masuch
[x] deduce output filetype from extension?
[x] is there echo in long rooms?
[?] fix bottom-band filtering
    * where should the bottom cutoff be? 45Hz maybe?
    * what sounds good?
[x] attenuators should probably return a trimmed-down impulse type
[x] remove universal hipass filtering, instead supply a way of changing the bottom-band hipass cutoff
[x] document the actual code - comments!

[x] check how portable the software is
    * **IMPORTANT**: I probably have to build the LIBRARIES I'm using for the
      correct deployment target, as well as program itself

[ ] add brdf
[ ] generate final samples
    * check damping coefficients - make some nice-sounding ones
[ ] why no comb filtering in square rooms?
[ ] compare results to commercial software

* multichannel example

Documentation
=============

* project readme
    * structure of project, where and what the other files are

* report
    * appendices w/ tests
