% Rayverb Distributable
% Reuben Thomas

Welcome to the binary distribution of the parallel_raytrace program!


License
=======

This software is licensed under the GNU GPL v2.
For full source code, visit the [github repo](https://github.com/reuk/parallel-reverb-raytracer).


System Requirements
===================

A Mac running OS X 10.9 or newer, with a discrete graphics card.


Package Layout
==============

* *bin*       - the program!
* *man*       - detailed documentation
* *demo*      - a script and some models to get you started producing reverbs
* *examples*  - some pre-rendered impulse response
* *readme.md* - this file


Getting Started
===============

First Run
---------

1.  Copy the entire contents of this disk image to somewhere on your hard drive.
    I suggest making a new folder in your Applications folder, and putting
    everything in there.

2.  Once you've copied everything to your hard drive, eject the disk image, then
    navigate to the copy of the `bin` folder on your drive.

3.  Next, open Terminal.app (or another terminal emulator). You can find this in
    `/Applications/Utilities`, or by opening Spotlight and typing 'Terminal'.

4.  In the terminal, type `cd`, drag the `bin` folder onto the terminal
    window, then press enter. This will switch the working directory to this
    folder.

5.  Now, you can run the program by typing:

    ```bash
    ./parallel_raytrace ../demo/assets/configs/bedroom.json ../demo/assets/test_models/bedroom.obj ../demo/assets/materials/mat.json output.aiff
    ```

    This will create a file called `output.aiff`, created by tracing the model
    `bedroom.obj` using the config file `bedroom.json` and the material file
    `mat.json`.

Generating Lots of Demos
------------------------

Lots of example impulses are given in the `examples/impulses` folder.
You can re-generate these all yourself if you want.

Now, in a Terminal window, type `cd`, then drop in the `demo` folder, then
press enter.
Finally, run `./gen.sh`.
This command just runs the parallel_raytrace program on lots and lots of
different inputs, and will probably run for a *long time*.
On my machine, each trace takes about 10 seconds, so generating all
100-or-so will probably take 15 or 20 minutes.

Listening to the Demos
----------------------

If you have Max MSP and the [HissTools](http://eprints.hud.ac.uk/14897/)
installed you can use `examples/test_convolver.maxpat` to listen to the impulse
responses in action.
More information on this patch is provided within the patch itself.


More Information
================

The pdf file in `man` gives a more in-depth introduction to the program, and
explains the input file formats in detail.
This file is a prettier version of the UNIX manpage for the program, which is
also included.

If you decide you can't live without the program and want to install it properly
on your machine, move `parallel_raytrace` from `bin` to `/usr/local/bin`, and
move `parallel_raytrace.1` from `man` to `/usr/share/man/man1`.
To uninstall, just delete these two files from those locations.

This program was written for the Individual Project component of my Music Tech
BA at Huddersfield University.

Questions? Bug reports? Contact me using the [github repo](https://github.com/reuk/parallel-reverb-raytracer).
