#include "rayverb.h"

const std::string diagnostic
#ifdef DIAGNOSTIC
("#define DIAGNOSTIC")
#endif
;

const std::string Scene::KERNEL_STRING (diagnostic + R"(

#define EPSILON (0.0001f)
#define NULL (0)

#define SPEED_OF_SOUND (340.0f)

constant float SECONDS_PER_METER = 1.0f / SPEED_OF_SOUND;
typedef float8 VolumeType;

typedef struct {
    float3 position;
    float3 direction;
} Ray;

typedef struct {
    VolumeType specular;
    VolumeType diffuse;
} Surface;

typedef struct {
    unsigned long surface;
    unsigned long v0;
    unsigned long v1;
    unsigned long v2;
} Triangle;

typedef struct {
    global Triangle * primitive;
    float distance;
} Intersection;

typedef struct {
    VolumeType volume;
    float3 direction;
    float time;
#ifdef DIAGNOSTIC
    float3 position;
#endif
} Impulse;

typedef struct {
    float3 direction;
    float coefficient;
} Speaker;

typedef struct {
    float3 v0;
    float3 v1;
    float3 v2;
} TriangleVerts;

float triangle_vert_intersection (float3 v0, float3 v1, float3 v2, Ray * ray);
float triangle_vert_intersection (float3 v0, float3 v1, float3 v2, Ray * ray)
{
    float3 e0 = v1 - v0;
    float3 e1 = v2 - v0;

    float3 pvec = cross (ray->direction, e1);
    float det = dot (e0, pvec);

    if (-EPSILON < det && det < EPSILON)
        return 0.0f;

    float invdet = 1.0f / det;
    float3 tvec = ray->position - v0;
    float ucomp = invdet * dot (tvec, pvec);

    if (ucomp < 0.0f || 1.0f < ucomp)
        return 0.0f;

    float3 qvec = cross (tvec, e0);
    float vcomp = invdet * dot (ray->direction, qvec);

    if (vcomp < 0.0f || 1.0f < vcomp + ucomp)
        return 0.0f;

    return invdet * dot (e1, qvec);
}

float triangle_intersection
(   global Triangle * triangle
,   global float3 * vertices
,   Ray * ray
);
float triangle_intersection
(   global Triangle * triangle
,   global float3 * vertices
,   Ray * ray
)
{
    return triangle_vert_intersection
    (   vertices [triangle->v0]
    ,   vertices [triangle->v1]
    ,   vertices [triangle->v2]
    ,   ray
    );
}

float3 triangle_verts_normal (TriangleVerts * t);
float3 triangle_verts_normal (TriangleVerts * t)
{
    float3 e0 = t->v1 - t->v0;
    float3 e1 = t->v2 - t->v0;

    return normalize (cross (e0, e1));
}

float3 triangle_normal
(   global Triangle * triangle
,   global float3 * vertices
);
float3 triangle_normal
(   global Triangle * triangle
,   global float3 * vertices
)
{
    TriangleVerts t =
    {   vertices [triangle->v0]
    ,   vertices [triangle->v1]
    ,   vertices [triangle->v2]
    };
    return triangle_verts_normal (&t);
}

float3 reflect (float3 normal, float3 direction);
float3 reflect (float3 normal, float3 direction)
{
    return direction - (normal * 2 * dot (direction, normal));
}

Ray ray_reflect (Ray * ray, float3 normal, float3 intersection);
Ray ray_reflect (Ray * ray, float3 normal, float3 intersection)
{
    return (Ray) {intersection, reflect (normal, ray->direction)};
}

Ray triangle_reflectAt
(   global Triangle * triangle
,   global float3 * vertices
,   Ray * ray
,   float3 intersection
);
Ray triangle_reflectAt
(   global Triangle * triangle
,   global float3 * vertices
,   Ray * ray
,   float3 intersection
)
{
    return ray_reflect
    (   ray
    ,   triangle_normal (triangle, vertices)
    ,   intersection
    );
}

Intersection ray_triangle_intersection
(   Ray * ray
,   global Triangle * triangles
,   unsigned long numtriangles
,   global float3 * vertices
);
Intersection ray_triangle_intersection
(   Ray * ray
,   global Triangle * triangles
,   unsigned long numtriangles
,   global float3 * vertices
)
{
    Intersection ret = {NULL, 0};

    for (unsigned long i = 0; i != numtriangles; ++i)
    {
        global Triangle * thisTriangle = triangles + i;
        float distance = triangle_intersection (thisTriangle, vertices, ray);
        if
        (   distance > EPSILON
        &&  (   (ret.primitive == NULL)
            ||  (ret.primitive != NULL && distance < ret.distance)
            )
        )
        {
            ret = (Intersection) {thisTriangle, distance};
        }
    }

    return ret;
}

bool anyAbove (VolumeType in, float thresh);
bool anyAbove (VolumeType in, float thresh)
{
    const unsigned long LIMIT = sizeof (VolumeType) / sizeof (float);
    for (int i = 0; i != LIMIT; ++i)
    {
        if (fabs (in [i]) > thresh)
        {
            return true;
        }
    }

    return false;
}

VolumeType air_attenuation_for_distance (float distance);
VolumeType air_attenuation_for_distance (float distance)
{
    const VolumeType AIR_COEFFICIENT =
    {   0.001 * -0.1
    ,   0.001 * -0.2
    ,   0.001 * -0.5
    ,   0.001 * -1.1
    ,   0.001 * -2.7
    ,   0.001 * -9.4
    ,   0.001 * -29.0
    ,   0.001 * -60.0
    };
    return float8 (1.0) * pow (M_E, distance * AIR_COEFFICIENT);
}

float power_attenuation_for_distance (float distance);
float power_attenuation_for_distance (float distance)
{
    return 1 / (distance * distance);
}

VolumeType attenuation_for_distance (float distance);
VolumeType attenuation_for_distance (float distance)
{
    return
    (   air_attenuation_for_distance (distance)
    *   power_attenuation_for_distance (distance)
    );
}

//  for each intersection up to a certain level
//      reflect the mic in all of the previous planes
//      check that a ray from source to mic intersects all intermediate surfaces
//      if it does, add impulse to output array

float3 mirror_point (float3 p, TriangleVerts * t);
float3 mirror_point (float3 p, TriangleVerts * t)
{
    float3 n = triangle_verts_normal (t);
    float dist = dot (n, p - t->v0);
    return p + -n * dist * 2;
}

TriangleVerts mirror_verts (TriangleVerts * in, TriangleVerts * t);
TriangleVerts mirror_verts (TriangleVerts * in, TriangleVerts * t)
{
    return (TriangleVerts)
    {   mirror_point (in->v0, t)
    ,   mirror_point (in->v1, t)
    ,   mirror_point (in->v2, t)
    };
}

#define NUM_IMAGE_SOURCE 10

kernel void raytrace
(   global float3 * directions
,   float3 position
,   global Triangle * triangles
,   unsigned long numtriangles
,   global float3 * vertices
,   float3 source
,   global Surface * surfaces
,   global Impulse * impulses
,   global Impulse * image_source
,   unsigned long outputOffset
)
{
    size_t i = get_global_id (0);

    //  This is really a recursive algorithm, but I've implemented it
    //  iteratively.
    //  These variables will be updated as the ray is traced.

    //  These variables relate to the ray itself, and are used for the diffuse
    //  trace.
    Ray ray = {source, directions [i]};
    float distance = 0;
    VolumeType volume = 1;

    //  These variables are for image_source approximation.
    TriangleVerts prev_primitives [NUM_IMAGE_SOURCE];
    float3 mic_reflection = position;

    for (unsigned long index = 0; index != outputOffset; ++index)
    {
        //  Check for an intersection between the current ray and all the
        //  scene geometry.
        Intersection closest = ray_triangle_intersection
        (   &ray
        ,   triangles
        ,   numtriangles
        ,   vertices
        );

        //  If there's no intersection, the ray's somehow shot into empty space
        //  and we should stop tracing.
        if (closest.primitive == NULL)
        {
            break;
        }

        global Triangle * triangle = closest.primitive;
        //  If we're fewer than NUM_IMAGE_SOURCE layers deep, the ray can be
        //  used to check for early reflections.
        if (index < NUM_IMAGE_SOURCE - 1)
        {
            //  Get the vertex data of the intersected triangle.
            TriangleVerts current =
            {   vertices [closest.primitive->v0]
            ,   vertices [closest.primitive->v1]
            ,   vertices [closest.primitive->v2]
            };

            //  For each of the previous triangles that have been intersected,
            //  reflect this triangle in those ones.
            for (unsigned int k = 0; k != index; ++k)
            {
                current = mirror_verts (&current, prev_primitives + index);
            }

            //  Now add the reflected triangle to the list of previously struck
            //  triangles.
            prev_primitives [index] = current;

            //  Reflect the previous microphone image in the newest triangle.
            mic_reflection = mirror_point (mic_reflection, &current);

            const float3 DIFF = mic_reflection - source;
            const float DIST = length (DIFF);
            const float3 DIR = normalize (DIFF);

            //  Check whether a ray from the source to the new microphone image
            //  intersects all of the intermediate triangles.
            Ray toMic = {source, DIR};
            bool intersects = true;
            for (unsigned int k = 0; k != index && intersects; ++k)
            {
                if
                (   triangle_vert_intersection
                    (   prev_primitives [k].v0
                    ,   prev_primitives [k].v1
                    ,   prev_primitives [k].v2
                    ,   &toMic
                    )
                <=  0
                )
                {
                    intersects = false;
                }
            }

            //  If the ray intersects all the triangles, and the distance from
            //  source to microphone image is less than a certain value,
            //  the reflection pattern is valid for an early reflection
            //  contribution.
            const float EARLY_REF_SECONDS = 10;
            if (intersects && DIST < SPEED_OF_SOUND * EARLY_REF_SECONDS)
            {
                image_source [i * NUM_IMAGE_SOURCE + index] = (Impulse)
                {   volume * (VolumeType) 0.0000001
                ,   -DIR
                ,   SECONDS_PER_METER * DIST
#ifdef DIAGNOSTIC
                ,   mic_reflection
#endif
                };
            }
        }

        float3 intersection = ray.position + ray.direction * closest.distance;
        float newDist = distance + closest.distance;
        VolumeType newVol = -volume * surfaces [triangle->surface].specular;

        const float3 vecToMic = position - intersection;
        const float mag = length (vecToMic);
        const float3 direction = normalize (vecToMic);
        Ray toMic = {intersection, direction};

        Intersection inter = ray_triangle_intersection
        (   &toMic
        ,   triangles
        ,   numtriangles
        ,   vertices
        );

        const bool IS_INTERSECTION = inter.primitive == NULL || inter.distance > mag;
        const float DIST = IS_INTERSECTION ? newDist + mag : 0;
        const float DIFF = fabs (dot (triangle_normal (triangle, vertices), direction));
        impulses [i * outputOffset + index] = (Impulse)
        {   (   IS_INTERSECTION
            ?   (   volume
                *   attenuation_for_distance (DIST)
                *   surfaces [triangle->surface].diffuse
                *   DIFF
                )
            :   0
            )
        ,   -direction
        ,   SECONDS_PER_METER * DIST
#ifdef DIAGNOSTIC
        ,   intersection
#endif
        };

        Ray newRay = triangle_reflectAt
        (   triangle
        ,   vertices
        ,   &ray
        ,   intersection
        );

        ray = newRay;
        distance = newDist;
        volume = newVol;
    }
}

float speaker_attenuation (Speaker * speaker, float3 direction);
float speaker_attenuation (Speaker * speaker, float3 direction)
{
    return
    (   (1 - speaker->coefficient)
    +   speaker->coefficient
    *   dot (normalize (direction), normalize (speaker->direction))
    );
}

kernel void attenuate
(   global Impulse * impulsesIn
,   global Impulse * impulsesOut
,   unsigned long outputOffset
,   Speaker speaker
)
{
    size_t i = get_global_id (0);
    const unsigned long END = (i + 1) * outputOffset;
    for (unsigned long j = i * outputOffset; j != END; ++j)
    {
        const float ATTENUATION = speaker_attenuation
        (   &speaker
        ,   impulsesIn [j].direction
        );
        impulsesOut [j] = (Impulse)
        {   impulsesIn [j].volume * ATTENUATION
        ,   impulsesIn [j].direction
        ,   impulsesIn [j].time
        };
    }
}

float3 transform (float3 pointing, float3 up, float3 d);
float3 transform (float3 pointing, float3 up, float3 d)
{
    float3 x = normalize(cross(up, pointing));
    float3 y = cross(pointing, x);
    float3 z = pointing;

    return (float3)
    (   dot(x, d)
    ,   dot(y, d)
    ,   dot(z, d)
    );
}

float azimuth (float3 d);
float azimuth (float3 d)
{
    return atan2(d.z, d.x);
}

float elevation (float3 d);
float elevation (float3 d)
{
    return atan2(d.y, length(d.xz));
}

VolumeType hrtf_attenuation
(   global VolumeType * hrtfData
,   float3 pointing
,   float3 up
,   float3 impulseDirection
);
VolumeType hrtf_attenuation
(   global VolumeType * hrtfData
,   float3 pointing
,   float3 up
,   float3 impulseDirection
)
{
    float3 transformed = transform(pointing, up, impulseDirection);

    unsigned long a = degrees(azimuth(transformed));
    unsigned long e = degrees(elevation(transformed));
    e = 90 - e;

    return hrtfData[a * 180 + e];
}

kernel void hrtf
(   global Impulse * impulsesIn
,   global Impulse * impulsesOut
,   unsigned long outputOffset
,   global VolumeType * hrtfData
,   float3 pointing
,   float3 up
)
{
    size_t i = get_global_id (0);
    const unsigned long END = (i + 1) * outputOffset;
    for (unsigned long j = i * outputOffset; j != END; ++j)
    {
        const VolumeType ATTENUATION = hrtf_attenuation
        (   hrtfData
        ,   pointing
        ,   up
        ,   impulsesIn [j].direction
        );

        impulsesOut [j] = (Impulse)
        {   impulsesIn [j].volume * ATTENUATION
        ,   impulsesIn [j].direction
        ,   impulsesIn [j].time
        };
    }
}

)");
