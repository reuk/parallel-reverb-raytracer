% Bounding Interval Hierarchy notes
% AAAARGH EW EW NO
% Notes from 'Instant Ray Tracing: The Bounding Interval Hierarchy' by Wachter
% and Keller

The Bounding Interval Hierarchy
===============================

Advantages
----------

* Simple
* Fast for static scenes
* High numerical precision

Disadvantages
-------------

* Not sure how it will cope with architectural models
    * i.e. models with long/wide triangles

Data Structure
--------------

* Just store two parallel planes perpendicular to the x, y, or z axis
    * As opposed to storing full bounding boxes for each node
* A node can look like this:

```
typedef struct
{
    uint32_t index;

    union
    {
        uint32_t items;
        float clip [2];
    }
} Node;
```

* Each inner node stores
    * an index pointing to two children
    * two float clipping planes
    * (the lowest bits of the index specify the axis of the planes)
        * alternatively we could store this in an enum because what the heck

Ray Intersection
----------------

* Possible to directly access the child that is closer to the ray origin by the
  sign of the ray direction
* Also possible to skip intersecting with two non-overlapping children
* Not possible to stop recursing as soon as an intersection is found, because
  'bounding boxes' (well, bounding planes) may overlap

Construction
------------

* Calculate a split plane
    * This is the hard bit
* Classify each object as 'left' or 'right' of the plane
    * I guess we use centroids for this?
    * 'depending on which side of the plane it overlaps more'
* Calculate partitioning plane values depending on the min/max coordinates
  of the objects on either side of the plane

Calculating Split Planes
------------------------

* Recursively subdivide the global bounding box at the mid-point of each longest
  side
* Object list is recursively partitioned
* Bounding boxes are aligned to object bounding boxes
* Recursion terminates when only one object is left
    * Or you can set more objects to be sufficient

Actual Construction Algorithm
-----------------------------

* Resolution of regular grid is determined by the ration of the scene bounds
  divided by the average object size
* Each grid cell consists of a counter, initialized to zero
* One corner of each object's bounding box is used to increment the counter for
  the grid cell that contains it
    * The number of all objects should equal the sum of all counters
* Transform counters to offsets by replacing each counter by the sum of all
  previous counters
* Global object index array is allocated
* Using the same point of every object, the objects can be sorted into the
  buckets using the offsets from the previous step
* Bounding box calculated for each bucket

