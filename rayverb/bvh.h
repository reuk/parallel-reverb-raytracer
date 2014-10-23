#pragma once

#define __CL_ENABLE_EXCEPTIONS
#include "cl.hpp"

//  I think we can build this in maybe one pass if we do a top-down breadth-
//  first construction

class Bvh
{
public:
    struct BvhNode
    {
        enum BvhType
        {   BVH_BOX
        ,   BVH_LEAF
        };

        BvhType type;
        
        union 
        {
            struct 
            {
                cl_float3 min;
                cl_float3 max;
                cl_ulong children;
                cl_ulong firstChild;
            };
            struct 
            {
                cl_ulong index;
            };
        };
    };

    Bvh();
    virtual ~Bvh();

private:


};
