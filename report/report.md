% Individual Project Report
% Matthew Reuben Thomas U1154964

Introduction
============

<!--
Project scope, goals, and domain. What is the problem?
-->

The goal of this project was to produce a piece of software which could
efficiently synthesize high-quality impulse responses for virtual environments.
The software had to run on commodity hardware, available to most musicians,
while also producing high-quality output at a reasonable speed.
In addition, the software had to be capable of rendering impulses suitable for
basic auralization, using HRTF-based techniques.

Literature Review
=================

<!--
Previous research, and what it's achieved.
What have you read?
What could you have done?
What did you decide to do?
-->

Many different implementations of modelling reverbs exist.
Some of these are commercially available, for example the Odeon family of room
acoustics software (discussed in @rindel2000), and the Catt-Acoustic program
(@dalenback2010).
Many more exist only as research projects without commerical implementations -
the programs described by @rober2007, @savioja2002, @schissler2014 and
@taylor2012 (to name a few) do not appear to be available in any form to the
general public.

Physically-based reverb algorithms can largely be divided into two categories,
namely wave-based and geometrical algorithms, each of which can be divided
into subcategories.

@savioja2002 notes that wave-based methods include the *finite element method*,
*boundary element method*, and the *finite-difference time-domain model*.
These methods produce very accurate results at specific frequencies, however
they are often very costly.
In these models, the scene being modeled is divided into a mesh, where the mesh
density is dictated by sampling frequency.
Calculations must be carried out for each node on the mesh, as a result of
which, computational load increases as a function of sampling frequency.
This in turn makes higher frequencies very computationally expensive to
calculate.

Geometric methods may be stochastic or deterministic, and are usually based on
some kind of ray-casting.
In these models, sound is supposed to act as a ray rather than a wave.
This representation is well suited to high frequency sounds, but is unable
to take the sound wavelength into account, often leading to overly accurate
higher-order reflections (@rindel2000).
These methods also often ignore wave effects such as interference or diffusion.

The most common stochastic method is ray tracing, which has two main advantages:
Firstly, an implementation can take influences from the extensive body of
research into graphical raytracing methods.
Secondly, raytracing is an 'embarrassingly parallel' algorithm, meaning that
it can easily be distributed across many processors simultaneously, as there is
no need for signalling between algorithm instances, and there is only a single
'final gather' (@stephens2013).

A common deterministic method is the image-source method.
This algorithm is conceptually very simple, and therefore fairly straightforward
to implement.
However, it quickly becomes very expensive, with complexity proportional to
the number of primitives in the scene raised to the power of the number of
reflections.
For scenes of a reasonable size, a great number of calculations are required,
making soley image-source based methods impractical for most purposes.

Many systems implement 'hybrid' algorithms, which may combine any of the above
methods.
Combining wave-based and geometric methods has an obvious benefit - the
wave-based simulation can be used to generate a low-frequency response, while
a geometric method can generate the higher-frequency portion.
Similarly, combining ray tracing and image-source modelling allows for the use
of accurate image-source simulation for just the early reflections, with
faster stochastic modelling of later reflections.

The majority of the systems mentioned are realtime - the programs mentioned by
@rober2007, @shroder2011, @schissler2014, @savioja2002, and @taylor2012, along
with the Catt-Acoustic software, are all capable of producing real-time
auralizations.
Offline renderers also exist. The Odeon software and the system described by
@kapralos2004 are both examples of offline renderers.

Realtime systems play an important role in audio engines for interactive games
or virtual-reality systems, where it is often necessary for virtual environments
to provide realistic sonic cues to the interacting user.
However, such systems must be capable of producing impulse responses very
rapidly, at least tens of times per second.
@savioja2002 and @kapralos2004 note that it is generally only possible to use
geometric methods to simulate early reflections at real-time rates, as modelling
later reflections becomes too computationally expensive.
@schissler2014 mentions that in benchmarks for their real-time system, they
traced 1000 rays from each source to an arbitrary depth of around 10
reflections.
@zuofu2014 describes a real-time geometric system for modelling sound in 3D
games, which is capable of rendering 4096 rays to a depth of 3 reflections in
real-time.

In raytracing, as with any other stochastic modelling method, better quality
is achieved primarily through taking more samples.
Clearly, the constraints that real-time processing places upon auralization
methods mean that impulses generated in this way will be inaccurate, and
likely of insufficient quality for music production.

The final notable difference between implementations is to do with hardware
rather than software.
Many modern implementations of raytracing algorithms take advantage of the
computer's graphics processing unit (GPU), which provides a huge number of
discrete processors that can run the same algorithm in parallel.
Though the processors are individually much less powerful than the typical
central processing unit (CPU), great speedups are possible due to the sheer
number of processes that can be run simultaneously.
Raytracing algorithms have the added advantage that most of the data supplied
to the algorithm is 'static' - all the information about the scene, such as
geometry, source position, mic position, and materials can be reused for each
ray that is fired.
The only variant parameter is the initial ray direction.
This means that the static data can be copied to graphics memory once, and then
referred to by every raytracer instance.
Due to the popularity of raytracing techniques in graphics, a variety of
papers are available concerning the implementation of raytracers on graphics
hardware.

<!-- TODO examples of programs using the GPU -->

On the other hand, many raytracing implementations just run on the CPU.
It is still possible to achieve reasonable speedups over naive implementations
by using single-instruction multiple-data (SIMD) instructions or multiple
threads.
Advantages of this approach are mostly to do with tooling.
GPU code must be written in a speciality language - 'general purpose' options
include Cuda and OpenCL, though Cuda code will only run on Nvidia GPUs.
Alternatively, a graphics shader language such as GLSL can be coerced into
interacting with non-graphical data.
These speciality languages often have very sparse programming environments,
and much better tools are available for working with and debugging CPU code
than GPU code.
Additionally, every platform is guaranteed to have a CPU on which the program
can run, but not every system will have a compatible GPU.

Implementation
==============

For this project, I opted to build an offline raytracing algorithm, which I
later adapted into a hybrid raytracing/image-source model.
I decided to keep the costliest and 'most parallel' parts of the algorithm on
the GPU where possible, doing only mixdown and postprocessing on the CPU.

<!--
Libraries and technologies I've used.
Code structure and algorithm design.
Rationale - why did you decide on this approach?
-->

Evaluation
==========

<!--
Progress throughout the project.
Problems you had and how you overcame them.
Specific problems, and testing methods.
What would you change or do differently in future projects?
What would you add?
Did the process work?
Known bugs.

Might be interesting to model sources - ray starting band volumes as a function
of ray direction.
Similarly, microphone modelling.
-->

Bibliography
============

---
nocite: |
    @wrapper2010, @antani2012, @battenberg2011, @belloch2011, @belloch2012, @brent2008, @cowan2009, @dammertz2008, @hulusic2012, @mullerTomfelde2001, @opencl2009, @raghuvanshi2009, @schissler2014, @shroder2011, @wei2012, @goldsmith1987, @taylor2012, @tsakok2009, @hradsky2011, @noisternig2008, @wnoisternig2008
...
