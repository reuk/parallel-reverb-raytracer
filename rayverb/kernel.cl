#define EPSILON (0.0001f)
#define NULL (0)
#define THRESHOLD (0.001f)

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
    unsigned long surface;
    float3 position;
    float3 normal;
    VolumeType volume;
    float distance;
} Reflection;

typedef struct {
    VolumeType volume;
    float3 direction;
    float time;
} Impulse;

typedef struct {
    float3 direction;
    float coefficient;
} Speaker;

typedef struct {
    int azimuth, elevation;
    VolumeType coefficients [2];
} Hrtf;


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
    float3 v0 = vertices [triangle->v0];
    float3 v1 = vertices [triangle->v1];
    float3 v2 = vertices [triangle->v2];
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

float3 triangle_normal
(   global Triangle * triangle
,   global float3 * vertices
);
float3 triangle_normal
(   global Triangle * triangle
,   global float3 * vertices
)
{
    float3 v0 = vertices [triangle->v0];
    float3 v1 = vertices [triangle->v1];
    float3 v2 = vertices [triangle->v2];
    float3 e0 = v1 - v0;
    float3 e1 = v2 - v0;

    return normalize (cross (e0, e1));
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
        if (distance > EPSILON &&
            ((ret.primitive == NULL) ||
             (ret.primitive != NULL && distance < ret.distance)))
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

constant VolumeType AIR_COEFFICIENT =
{   0.001 * -0.1
,   0.001 * -0.2
,   0.001 * -0.5
,   0.001 * -1.1
,   0.001 * -2.7
,   0.001 * -9.4
,   0.001 * -29.0
,   0.001 * -60.0
};

VolumeType air_attenuation_for_distance (float distance);
VolumeType air_attenuation_for_distance (float distance)
{
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

kernel void raytrace
(   global float3 * directions
,   float3 position
,   global Triangle * triangles
,   unsigned long numtriangles
,   global float3 * vertices
,   float3 source
,   global Surface * surfaces
,   global Impulse * impulses
,   unsigned long outputOffset
)
{
    size_t i = get_global_id (0);

    Ray ray = {source, directions [i]};
    float distance = 0;
    VolumeType volume = 1;

    for
    (   unsigned long index = 0
    ;   /*anyAbove (volume, THRESHOLD) && */index != outputOffset
    ;   ++index
    )
    {
        Intersection closest = ray_triangle_intersection
        (   &ray
        ,   triangles
        ,   numtriangles
        ,   vertices
        );

        if (closest.primitive == NULL)
        {
            break;
        }

        float3 intersection = ray.position + ray.direction * closest.distance;
        float newDist = distance + closest.distance;

        global Triangle * triangle = closest.primitive;
        VolumeType newVol = -volume * surfaces [triangle->surface].specular;

        Reflection reflection =
        {   triangle->surface
        ,   intersection
        ,   triangle_normal (triangle, vertices)
        ,   newVol
        ,   newDist
        };

        float3 vecToMic = position - reflection.position;
        float mag = length (vecToMic);
        float3 direction = normalize (vecToMic);
        Ray toMic = {intersection, direction};

        Intersection inter = ray_triangle_intersection
        (   &toMic
        ,   triangles
        ,   numtriangles
        ,   vertices
        );

        if (inter.primitive == NULL || inter.distance > mag)
        {
            const float DIST = reflection.distance + inter.distance;
            const float TIME = SECONDS_PER_METER * DIST;
            const float DIFF = dot (reflection.normal, -direction);
            if (DIFF > 0)
            {
                impulses [i * outputOffset + index] = (Impulse)
                {   (   volume
                    *   attenuation_for_distance (DIST)
                    *   surfaces [reflection.surface].diffuse
                    *   DIFF
                    )
                ,   -direction
                ,   TIME
                };
            }
        }

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

float azimuth (float3 d)
{
    return atan2(d.y, d.x);
}

float elevation (float3 d)
{
    return atan2(d.z, length(d.xy));
}

VolumeType hrtf_attenuation
(   global Hrtf * hrtfData
,   unsigned long hrtfLen
,   float3 pointing
,   float3 up
,   float3 impulseDirection
,   unsigned long channel
)
{
    float3 transformed = transform(pointing, up, impulseDirection);
    float a = fmod(degrees(azimuth(transformed)) + 360, 360);
    float e = fmod(degrees(elevation(transformed)) + 360, 360);

    VolumeType vt = (VolumeType) (0);

    for (unsigned long i = 1; i != hrtfLen; ++i)
    {
        if
        (   a < hrtfData [i].azimuth
        &&  hrtfData [i - 1].elevation <= e
        &&  e < hrtfData [i].elevation
        )
        {
            vt = hrtfData [i].coefficients [channel];
            break;
        }
    }

    return vt;
}

kernel void hrtf
(   global Impulse * impulsesIn
,   global Impulse * impulsesOut
,   unsigned long outputOffset
,   global Hrtf * hrtfData
,   unsigned long hrtfLen
,   float3 pointing
,   float3 up
,   unsigned long channel
)
{
    size_t i = get_global_id (0);
    const unsigned long END = (i + 1) * outputOffset;
    for (unsigned long j = i * outputOffset; j != END; ++j)
    {
        const VolumeType ATTENUATION = hrtf_attenuation
        (   hrtfData
        ,   hrtfLen
        ,   pointing
        ,   up
        ,   impulsesIn [j].direction
        ,   channel
        );
        impulsesOut [j] = (Impulse)
        {   impulsesIn [j].volume * ATTENUATION
        ,   impulsesIn [j].direction
        ,   impulsesIn [j].time
        };
    }
}
