% Notes from 'Grid-based SAH BVH construction on a GPU'
% because otherwise I won't remember any of it

BVH Construction Algorithm
--------------------------

* Binary BVH of axis aligned bounding boxes
* BVH constructed in several levels
* Primitives ordered in such a way that each grid cell can encode a range over
  the primitives

Vague Process
-------------

* Inner node divided into 8^3 cells
* 21 split planes evaluated
    * one at every cell boundary inside the root node in each of 3 dimensions
* Best one selected
* Primitives partitioned according to the best split plane
* Then for each of the two resulting subsets we find new split positions
    * How? 
        * Are we still constrained to the same 8^3 grid? 
        * Do we construct a new one?

Grid Stuff
----------

* The scene bounds (AABB) are used as the bounds of the top-level node
* Resolution is 1024^3
    * Decomposed into two levels to save storage space (most cells are empty)
        * Top level: 128^3
        * Each bottom level: 8^3

Triangle Stuff
--------------

* Each triangle represented by an ID and a bounding box over it
    * or a part of it
* Reference array is filled with sequential IDs
* Compute a 30-bit cell id of every triangle centroid (using the 1024^3 grid)
    * First 21 bits = top level cell
    * Last 9 bits = bottom level cell 
* Radix sort the sequential ID array by cell ids.
    * Can do this in parallel on the GPU probably
* Representation can now be compressed
    * Into ref begin + length for each cell

Triangle Distribution Process
-----------------------------

* Each triangle given an index number
    * indexes
* Centroid of each triangle calculated
    * triangles
    * synced array of triangle centroids
* Work out the cell id of each centroid
    * synced array of cell ids
* Sort the arrays of indexes and cell ids by cell id
    * cell ids
    * synced array of triangle indexes
* Triangles in the same outer grid grouped together
    * new array: tuples of (refsBegin, numRefs)
        * points at the cell ids and triangle indexes arrays
* Full outer grid created
    * new array: full size of the top grid
        * tuples of (refsBegin, numRefs, inner id)
* Compacted inner grid created
    * new array: oh I don't know


ugh
