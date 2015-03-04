float azimuth (PVector d)
{
  return atan2 (d.x, d.z);
}

float elevation (PVector d)
{
  return atan2 (d.y, sqrt (d.x * d.x + d.z * d.z));
}

PVector transform (PVector pointing, PVector up, PVector d)
{
  PVector x = up.cross (pointing);
  x.normalize();
  
  PVector y = pointing.cross (x);
  y.normalize();
  
  PVector z = pointing;
  z.normalize();
  
  return new PVector (x.dot (d), y.dot (d), z.dot (d));
}

void attenuation (PVector pointing, PVector up, PVector d)
{
  PVector transformed = transform (pointing, up, d);
  
  float a = degrees (azimuth (transformed));
  a += 180;
  float e = degrees (elevation (transformed));
  e = 90 - e;
  
  println (a, e);
}

void setup()
{
  size (500, 500, P3D);
  colorMode (RGB, 1);
}

void draw()
{
  background (0);
  final float ANGLE = 0;
  PVector d = new PVector (sin (ANGLE), -2, cos (ANGLE));
  d.normalize();
  attenuation (new PVector (0, 0, 1), new PVector (0, 1, 0), d);
}
