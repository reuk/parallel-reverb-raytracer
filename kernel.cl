#define EPSILON (0.0001f)
#define NULL (0)
#define THRESHOLD (0.001f)

#define SPEED_OF_SOUND (340.0f)
constant float SECONDS_PER_METER = 1.0f / SPEED_OF_SOUND;

//  struct definitions

typedef struct {
    float3 position;
    float3 direction;
} Ray;

typedef struct {
    global void * primitive;
    float distance;
} Intersection;

typedef struct {
    float3 specular;
    float3 diffuse;
} Surface;

typedef struct {
    unsigned long surface;
    unsigned long v0;
    unsigned long v1;
    unsigned long v2;
} Triangle;

typedef struct {
    unsigned long surface;
    float3 origin;
    float radius;
} Sphere;

typedef struct {
    unsigned long surface;
    float3 position;
    float3 normal;
    float3 volume;
    float distance;
} Reflection;

typedef struct {
    float3 volume;
    float time;
} Impulse;

typedef struct {
    float3 direction;
    float coefficient;
} Speaker;

//  implementations

float triangle_intersection (global Triangle * triangle,
                             global float3 * vertices,
                             Ray * ray)
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

float3 triangle_normal (global Triangle * triangle,
                        global float3 * vertices)
{
    float3 v0 = vertices [triangle->v0];
    float3 v1 = vertices [triangle->v1];
    float3 v2 = vertices [triangle->v2];
    float3 e0 = v1 - v0;
    float3 e1 = v2 - v0;
    
    return normalize (cross (e0, e1));
}

float3 reflect (float3 normal, float3 direction)
{
    return direction - (normal * 2 * dot (direction, normal));
}

Ray ray_reflect (Ray * ray, float3 normal, float3 intersection)
{
    return (Ray) {intersection, reflect (normal, ray->direction)};
}

Ray triangle_reflectAt (global Triangle * triangle,
                        global float3 * vertices,
                        Ray * ray,
                        float3 intersection)
{
    return ray_reflect (ray,
                        triangle_normal (triangle, vertices),
                        intersection);
}

float sphere_intersection (global Sphere * sphere, Ray * ray)
{
    float3 sub = ray->position - sphere->origin;
    float a_comp = dot (ray->direction, ray->direction);
    float b_comp = 2 * dot (ray->direction, sub);
    float c_comp = dot (sub, sub) - pow (sphere->radius, 2);
    
    float disc = (b_comp * b_comp) - (4 * a_comp * c_comp);
    
    float ret = 0;
    
    if (disc > 0)
        ret = -(b_comp + sqrt (disc)) / (2 * a_comp);
    
    return ret;
}

Intersection ray_triangle_intersection (Ray * ray,
                                        global Triangle * triangles,
                                        unsigned long numtriangles,
                                        global float3 * vertices)
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

Intersection ray_sphere_intersection (Ray * ray, global Sphere * sphere)
{
    Intersection ret = {NULL, 0};
    float distance = sphere_intersection (sphere, ray);
    if (distance > EPSILON && ret.primitive == NULL)
    {
        ret = (Intersection) {sphere, distance};
    }
    return ret;
}

bool anyAbove (float3 in, float thresh)
{
    for (int i = 0; i != 3; ++i)
    {
        if (fabs (in [i]) > thresh)
        {
            return true;
        }
    }
    
    return false;
}

Intersection ray_intersection (Ray * ray,
                               global Sphere * sphere,
                               global Triangle * triangles,
                               unsigned long numtriangles,
                               global float3 * vertices)
{    
    Intersection si = ray_sphere_intersection (ray, sphere);
    Intersection ti = ray_triangle_intersection (ray,
                                                 triangles,
                                                 numtriangles,
                                                 vertices);
    
    if (si.primitive && ti.primitive)
        return si.distance < ti.distance ? si : ti;
    else if (si.primitive)
        return si;
    else if (ti.primitive)
        return ti;

    return (Intersection) {NULL, 0};
}

void test_value (global Impulse * impulse, float f)
{
    *impulse = (Impulse) {(float3) (f), f};
}

kernel void raytrace (global float3 * directions,
                      float3 position,
                      global Triangle * triangles,
                      unsigned long numtriangles,
                      global float3 * vertices,
                      global Sphere * sphere,
                      global Surface * surfaces,
                      global Impulse * impulses,
                      unsigned long outputOffset)
{
    size_t i = get_global_id (0);
    
    Ray ray = {position, directions [i]};
    float distance = 0;
    float3 volume = 1;
    unsigned long index = 0;

    while (anyAbove (volume, THRESHOLD) && index != outputOffset)
    {
        Intersection closest = ray_intersection (&ray,
                                                 sphere,
                                                 triangles,
                                                 numtriangles,
                                                 vertices);
        
        if (closest.primitive == NULL)
        {
            break;
        }
        
        if (closest.primitive == sphere)
        {
            float newDist = distance + closest.distance;
            
            Impulse impulse = {volume, SECONDS_PER_METER * newDist};
            impulses [i * outputOffset + index] = impulse;
            
            break;
        }
        
        
        float3 intersection = ray.position + ray.direction * closest.distance;

        float newDist = distance + closest.distance;

        global Triangle * triangle = (global Triangle *) closest.primitive;
        float3 newVol = -volume * surfaces [triangle->surface].specular;

        Reflection reflection = {
            triangle->surface,
            intersection,
            triangle_normal (triangle, vertices),
            newVol,
            newDist
        };

        float3 direction = normalize (sphere->origin - reflection.position);
        
        Ray toSource = {intersection, direction};
        
        Intersection inter = ray_intersection (&toSource,
                                               sphere,
                                               triangles,
                                               numtriangles,
                                               vertices);
        
        if (inter.primitive == sphere)
        {
            const float time = SECONDS_PER_METER * (reflection.distance + inter.distance);
            const float3 volume = reflection.volume * surfaces [reflection.surface].diffuse * dot (reflection.normal, direction);
            impulses [i * outputOffset + index] = (Impulse) {volume, time};

            ++index;
        }

        Ray newRay = triangle_reflectAt (triangle, vertices, &ray, intersection);

        ray = newRay;
        distance = newDist;
        volume = newVol;
    }
}

float speaker_attenuation (Speaker * speaker, float3 direction)
{
    return (1 - speaker->coefficient) + speaker->coefficient * dot (normalize (direction), normalize (speaker->direction));
}

kernel void attenuate (global Impulse * impulsesIn,
                       global Impulse * impulsesOut,
                       unsigned long outputOffset,
                       global float3 * directions,
                       Speaker speaker)
{
    size_t i = get_global_id (0);
    const float ATTENUATION = speaker_attenuation (&speaker, directions [i]);
    const unsigned long END = (i + 1) * outputOffset;
    for (unsigned long j = i * outputOffset; j != END; ++j)
    {
        impulsesOut [j].time = impulsesIn [j].time;
        impulsesOut [j].volume = impulsesIn [j].volume * ATTENUATION;
    }
}
