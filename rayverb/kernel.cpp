#include "rayverb.h"

const std::string Scene::KERNEL_STRING (
#ifdef DIAGNOSTIC
"#define DIAGNOSTIC\n"
#endif
"#define NUM_IMAGE_SOURCE " + std::to_string (NUM_IMAGE_SOURCE) + "\n"
"#define SPEED_OF_SOUND " + std::to_string (SPEED_OF_SOUND) + "\n"
R"(

#define EPSILON (0.0001f)
#define NULL (0)

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
    unsigned long primitive;
    float distance;
    bool intersects;
} Intersection;

typedef struct {
    VolumeType volume;
    float3 position;
    float time;
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

float3 triangle_normal (global Triangle * triangle, global float3 * vertices);
float3 triangle_normal (global Triangle * triangle, global float3 * vertices)
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
    Intersection ret = {0, 0, false};

    for (unsigned long i = 0; i != numtriangles; ++i)
    {
        global Triangle * thisTriangle = triangles + i;
        float distance = triangle_intersection (thisTriangle, vertices, ray);
        if
        (   distance > EPSILON
        &&  (   !ret.intersects
            ||  (ret.intersects && distance < ret.distance)
            )
        )
        {
            ret = (Intersection) {i, distance, true};
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

void mirror_point (float3 * p, TriangleVerts * t);
void mirror_point (float3 * p, TriangleVerts * t)
{
    float3 n = triangle_verts_normal (t);
    *p += -n * dot (n, *p - t->v0) * 2;
}

void mirror_verts (TriangleVerts * in, TriangleVerts * t);
void mirror_verts (TriangleVerts * in, TriangleVerts * t)
{
    mirror_point (&in->v0, t);
    mirror_point (&in->v1, t);
    mirror_point (&in->v2, t);
}

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
,   global unsigned long * image_source_index
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

    const float3 INIT_DIFF = mic_reflection - source;
    const float INIT_DIST = length (INIT_DIFF);
    image_source [i * NUM_IMAGE_SOURCE] = (Impulse)
    {   volume * attenuation_for_distance (INIT_DIST)
    ,   source
    ,   SECONDS_PER_METER * INIT_DIST
    };
    image_source_index [i * NUM_IMAGE_SOURCE] = 0;

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
        if (! closest.intersects)
        {
            break;
        }

        global Triangle * triangle = triangles + closest.primitive;
        //  If we're fewer than NUM_IMAGE_SOURCE layers deep, the ray can be
        //  used to check for early reflections.
        if (index < NUM_IMAGE_SOURCE - 1)
        {
            //  Get the vertex data of the intersected triangle.
            TriangleVerts current =
            {   vertices [triangle->v0]
            ,   vertices [triangle->v1]
            ,   vertices [triangle->v2]
            };

            //  For each of the previous triangles that have been intersected,
            //  reflect this triangle in those ones.
            for (unsigned int k = 0; k != index; ++k)
            {
                mirror_verts (&current, prev_primitives + k);
            }

            //  Now add the reflected triangle to the list of previously struck
            //  triangles.
            prev_primitives [index] = current;

            //  Reflect the previous microphone image in the newest triangle.
            mirror_point (&mic_reflection, &current);

            const float3 DIFF = mic_reflection - source;
            const float DIST = length (DIFF);
            const float3 DIR = normalize (DIFF);

            //  Check whether a ray from the source to the new microphone image
            //  intersects all of the intermediate triangles.
            Ray toMic = {source, DIR};
            bool intersects = true;
            for (unsigned long k = 0; k != index + 1; ++k)
            {
                const float TO_INTERSECTION = triangle_vert_intersection
                (   prev_primitives [k].v0
                ,   prev_primitives [k].v1
                ,   prev_primitives [k].v2
                ,   &toMic
                );

                if (TO_INTERSECTION <= EPSILON)
                {
                    intersects = false;
                    break;
                }
            }

            if (intersects)
            {
                float3 prevIntersection = source;
                for (unsigned long k = 0; k != index + 1; ++k)
                {
                    const float TO_INTERSECTION = triangle_vert_intersection
                    (   prev_primitives [k].v0
                    ,   prev_primitives [k].v1
                    ,   prev_primitives [k].v2
                    ,   &toMic
                    );

                    float3 intersectionPoint = source + DIR * TO_INTERSECTION;
                    for (long l = k - 1; l != -1; --l)
                    {
                        mirror_point (&intersectionPoint, prev_primitives + l);
                    }

                    Ray intermediate = {prevIntersection, normalize (intersectionPoint - prevIntersection)};
                    Intersection inter = ray_triangle_intersection
                    (   &intermediate
                    ,   triangles
                    ,   numtriangles
                    ,   vertices
                    );

                    const bool IS_INTERSECTION = inter.intersects && all ((intermediate.position + intermediate.direction * inter.distance) == intersectionPoint);

                    if (!IS_INTERSECTION)
                    {
                        intersects = false;
                        break;
                    }

                    prevIntersection = intersectionPoint;
                }

                if (intersects)
                {
                    float3 intersectionPoint = position;

                    Ray intermediate = {prevIntersection, normalize (intersectionPoint - prevIntersection)};
                    Intersection inter = ray_triangle_intersection
                    (   &intermediate
                    ,   triangles
                    ,   numtriangles
                    ,   vertices
                    );

                    const bool IS_INTERSECTION = (!inter.intersects) || (inter.distance > length (intersectionPoint - prevIntersection));

                    if (!IS_INTERSECTION)
                    {
                        intersects = false;
                    }
                }
            }

            //  If the ray intersects all the triangles, and the distance from
            //  source to microphone image is less than a certain value,
            //  the reflection pattern is valid for an early reflection
            //  contribution.
            if (intersects)
            {
                image_source [i * NUM_IMAGE_SOURCE + index + 1] = (Impulse)
                {   volume * attenuation_for_distance (DIST)
                ,   position + source - mic_reflection
                ,   SECONDS_PER_METER * INIT_DIST
                };
                image_source_index [i * NUM_IMAGE_SOURCE + index + 1] = closest.primitive + 1;
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

        const bool IS_INTERSECTION = (!inter.intersects) || inter.distance > mag;
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
        ,   intersection
        ,   SECONDS_PER_METER * DIST
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

float3 directionFromPosition (float3 position, float3 mic);
float3 directionFromPosition (float3 position, float3 mic)
{
    return normalize (mic - position);
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
(   float3 mic_pos
,   global Impulse * impulsesIn
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
        ,   directionFromPosition (impulsesIn [j].position, mic_pos)
        );
        impulsesOut [j] = (Impulse)
        {   impulsesIn [j].volume * ATTENUATION
        ,   impulsesIn [j].position
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
(   float3 mic_pos
,   global Impulse * impulsesIn
,   global Impulse * impulsesOut
,   unsigned long outputOffset
,   global VolumeType * hrtfData
,   float3 pointing
,   float3 up
,   unsigned long channel
)
{
    size_t i = get_global_id (0);
    const float WIDTH = 0.1;

    float3 ear_pos = transform
    (   pointing
    ,   up
    ,   (float3) {channel == 0 ? -WIDTH : WIDTH, 0, 0}
    ) + mic_pos;

    const unsigned long END = (i + 1) * outputOffset;
    for (unsigned long j = i * outputOffset; j != END; ++j)
    {
        const VolumeType ATTENUATION = hrtf_attenuation
        (   hrtfData
        ,   pointing
        ,   up
        ,   directionFromPosition (impulsesIn [j].position, mic_pos)
        );

        const float dist0 = distance (impulsesIn [j].position, mic_pos);
        const float dist1 = distance (impulsesIn [j].position, ear_pos);
        const float diff = dist1 - dist0;

        impulsesOut [j] = (Impulse)
        {   impulsesIn [j].volume * ATTENUATION
        ,   impulsesIn [j].position
        ,   impulsesIn [j].time + diff * SECONDS_PER_METER
        };
    }
}

)");
