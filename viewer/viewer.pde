import saito.objloader.*;
import org.json.*;

PImage logo;

OBJModel model;

class Intersection
{
  Intersection (PVector position, PVector volume)
  {
    _position = position;
    _volume = volume;
  }
  
  public PVector _position;
  public PVector _volume;
}

ArrayList <ArrayList <Intersection>> trace = new ArrayList <ArrayList <Intersection>>();

void setup()
{
  size (500, 500, P3D);
  colorMode (RGB, 1);
  frameRate (25);
  
  logo = loadImage("logo.png");

  model = new OBJModel (this, "/Users/reuben/Desktop/basic.obj");

  //BufferedReader reader = createReader ("/Users/reuben/Desktop/py_test_p.json");
  BufferedReader reader = createReader ("/Users/reuben/dev/parallel_raytrace/build/bin/out.dump");

  boolean read = true;
  while (read)
  {    
    try
    {
      String line = reader.readLine();
      JSON json = JSON.parse (line);
      ArrayList <Intersection> list = new ArrayList <Intersection>();
      list.add (new Intersection (new PVector (-5, 5, -5), new PVector (1, 1, 1)));
      for (int i = 0; i != json.length(); ++i)
      {
        JSON vec = json.getJSON (i).getJSON ("position");
        PVector pvec = new PVector (vec.getFloat (0), vec.getFloat (1), vec.getFloat (2));
        pvec.y = -pvec.y;

        JSON vol = json.getJSON (i).getJSON ("volume");
        PVector pvol = new PVector (abs (vol.getFloat (0)), abs (vol.getFloat (1)), abs (vol.getFloat (2)));

        list.add (new Intersection (pvec, pvol));
      }

      trace.add (list);
    }
    catch (Exception e)
    {
      println (e);
      read = false;
    }
  }
  
  println (trace.size());
}

void vertex (PVector p) 
{
  vertex (p.x, p.y, p.z);
}

void line (PVector a, PVector b)
{
  line (a.x, a.y, a.z, b.x, b.y, b.z);
}

void stroke (PVector c)
{
  stroke (c.x, c.y, c.z);
}

void draw()
{
  background (0.5
  );
  translate (width / 2, height / 2);
  rotateY (frameCount * TWO_PI * 0.01);
  scale (8);
  
  pushMatrix();
  fill (1, 0, 0);
  noStroke();
  translate (-5, 5, -5);
  sphere (1);
  popMatrix();
  
  pushMatrix();
  fill (0, 1, 1);
  noStroke();
  translate (5, -5, 5);
  sphere (1);
  popMatrix();

  noFill();
  strokeWeight (0.1);

  for (int i = 0; i != model.getSegmentCount (); ++i)
  {
    Face[] face = model.getSegment (i).getFaces();

    for (int j = 0; j != face.length; ++j)
    {
      PVector[] vertex = face [j].getVertices();

      stroke (0, 0, 0);

      beginShape();

      for (int k = 0; k != vertex.length; ++k)
      {
        vertex (vertex [k]);
      }

      endShape (CLOSE);

      PVector[] normal = face [j].getNormals();

      beginShape (LINES);
      for (int k = 0; k != vertex.length; ++k)
      {
        stroke (abs (normal [k].x), abs (normal [k].y), abs (normal [k].z));

        vertex (vertex [k]);
        vertex (PVector.add (vertex [k], PVector.mult (normal [k], 1)));
      }
      endShape();
    }
  }

  strokeWeight (0.1);

  ArrayList <Intersection> list = (ArrayList <Intersection>) trace.get (mouseX);

  beginShape();
  for (int j = 0; j != list.size(); ++j)
  {
    stroke (((Intersection) list.get (j))._volume);
    vertex (((Intersection) list.get (j))._position);
  }
  endShape();
  
  camera();
  image(logo, width - logo.width, height - logo.height);
  
//  saveFrame ("out-###.png");
}
